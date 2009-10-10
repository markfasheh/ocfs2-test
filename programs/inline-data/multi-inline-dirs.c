/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi-inline-dirs.c
 *
 * A mpi compatible program to verify inline directory data
 * concurently among multiple nodes.
 *
 * All tests read back the entire directory to verify correctness.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <ocfs2/ocfs2.h>

#include <dirent.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <linux/types.h>

#include <mpi.h>

#define OCFS2_MAX_FILENAME_LEN		255
#define HOSTNAME_MAX_SZ			255
#define MAX_DIRENTS			1024

#define FILE_BUFFERED_RW_FLAGS  (O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define WORK_PLACE                      "inline-data-test"
#define SHARED_MMAP_DIRENTS_FILE        ".shared_mmap_dirents_file"
#define SHARED_MMAP_NUM_FILE            ".shared_mmap_num_file"

struct my_dirent {
	unsigned int	type;
	unsigned int	name_len;
	unsigned int	seen;
	char		name[OCFS2_MAX_FILENAME_LEN];
};

static char *prog;
static char device[100];
static char uuid[100];

static ocfs2_filesys *fs;
static struct ocfs2_super_block *ocfs2_sb;

static unsigned long page_size;
static unsigned int blocksize = 4096;
static unsigned long clustersize;

unsigned int id_count;
unsigned long i_size;

static char mount_point[OCFS2_MAX_FILENAME_LEN];
static char work_place[OCFS2_MAX_FILENAME_LEN];
static char dirent_name[OCFS2_MAX_FILENAME_LEN];
static char dir_name[OCFS2_MAX_FILENAME_LEN];
static char shared_mmap_dirents_file[OCFS2_MAX_FILENAME_LEN];
static char shared_mmap_num_file[OCFS2_MAX_FILENAME_LEN];
static char *mmap_shared_dirents_region;
static char *mmap_shared_num_region;
static unsigned long mmap_dirents_size;
static unsigned long mmap_num_size;
static int mmap_dirents_fd;
static int mmap_num_fd;

static int iteration = 1;
static unsigned int operated_entries = 20;

struct my_dirent *dirents;
unsigned int *num_dirents;

static unsigned int max_inline_size;
unsigned int usable_space;

static char path[PATH_MAX];
static char path1[PATH_MAX];

static int testno = 1;

static int rank = -1, size;
static char hostname[HOSTNAME_MAX_SZ];

static unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

static inline char rand_char(void)
{
	return 'A' + (char) get_rand(0, 25);
}

static void usage(void)
{
	printf("Usage: multi-inline-dirs [-i <iteration>] [-s operated_entries] "
	       "<-u <uuid>> <mount_point>\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "dirs with inline data.\n\n"
	       "iteration specify the running times.\n"
	       "operated_dir_entries specify the entires number to be "
	       "operated,such as random create/unlink/rename.\n"
	       "uuid and mount_point are mandatory.\n");

	MPI_Finalize();
	exit(1);
}

static void abort_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void root_printf(const char *fmt, ...)
{
	va_list ap;

	if (rank == 0) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
	}
}

static int parse_opts(int argc, char **argv)
{
	int c;
	while (1) {
		c = getopt(argc, argv, "U:u:I:i:S:s:");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
		case 'I':
			iteration = atol(optarg);
			break;
		case 'u':
		case 'U':
			strcpy(uuid, optarg);
			break;
		case 's':
		case 'S':
			operated_entries = atol(optarg);
		default:
			break;
		}
	}

	if (strcmp(uuid, "") == 0)
		return EINVAL;

	if (argc - optind != 1)
		return EINVAL;

	strcpy(mount_point, argv[optind]);
	if (mount_point[strlen(mount_point) - 1] == '/')
		mount_point[strlen(mount_point) - 1] = '\0';

	return 0;
}

static int is_dot_entry(struct my_dirent *dirent)
{
	if (dirent->name_len == 1 && dirent->name[0] == '.')
		return 1;
	if (dirent->name_len == 2 && dirent->name[0] == '.'
	    && dirent->name[1] == '.')
		return 1;
	return 0;
}

static int unlink_dirent(struct my_dirent *dirent)
{
	sprintf(path1, "%s/%s", dir_name, dirent->name);

	return unlink(path1);
}

static void destroy_dir(void)
{
	int ret, i;
	struct my_dirent *dirent;

	for (i = 0; i < *num_dirents; i++) {
		dirent = &dirents[i];

		if (dirent->name_len == 0)
			continue;

		if (!is_dot_entry(dirent)) {
			ret = unlink_dirent(dirent);
			if (ret) {
				ret = errno;
				abort_printf("unlink failure %d: %s\n", ret,
					      strerror(ret));
			}
		}

		dirent->name_len = 0;
	}

	ret = rmdir(dir_name);
	if (ret) {
		ret = errno;
		abort_printf("rmdir failure %d: %s\n", ret,
			     strerror(ret));
	}
}

static struct my_dirent *find_my_dirent(char *name)
{
	int i, len;
	struct my_dirent *my_dirent;

	len = strlen(name);

	for (i = 0; i < *num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->name_len == 0)
			continue;

		if (my_dirent->name_len == len &&
		    strcmp(my_dirent->name, name) == 0)
			return my_dirent;
	}

	return NULL;
}

static void create_and_prep_dir(void)
{
	int ret;
	struct my_dirent *dirent;

	memset(dirents, 0, sizeof(struct my_dirent) * MAX_DIRENTS);

	dirent = &dirents[0];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 1;
	strcpy(dirent->name, ".");

	dirent = &dirents[1];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 2;
	strcpy(dirent->name, "..");

	*num_dirents = 2;

	ret = mkdir(dir_name, FILE_MODE);
	if (ret) {
		ret = errno;
		abort_printf("mkdir failure %d: %s\n", ret, strerror(ret));
	}
}

static void create_file(char *filename)
{
	int ret, fd;
	struct my_dirent *dirent;

	dirent = &dirents[*num_dirents];
	*num_dirents += 1;

	dirent->type = S_IFREG >> S_SHIFT;
	dirent->name_len = strlen(filename);
	dirent->seen = 0;
	strcpy(dirent->name, filename);

	sprintf(path, "%s/%s", dir_name, dirent->name);

	fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
	if (fd == -1) {
		ret = errno;
		abort_printf("open failure %d: %s\n", ret,
			     strerror(ret));
	}

	close(fd);
}

static void create_files(char *prefix, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		sprintf(path1, "%s%011d", prefix, i);
		create_file(path1);
	}
}
static int get_max_inlined_entries(int max_inline_size)
{
	unsigned int almost_full_entries;

	/*
	* This will create enough entries to leave only 280 free
	* bytes in the directory.
	*
	* max_inline_size % 512 = 312    [always]
	* rec overhead for '.' and '..' = 32
	* So, 312 - 32 = 280.
	*
	*/
	almost_full_entries = max_inline_size / 512;
	almost_full_entries *= 512;
	almost_full_entries /= 32;

	/*
	* Now we add enough 32 byte entries to fill that remaining 280 bytes:
	*
	* 280 / 32 = 8
	*
	* And we'll be left over with 24 bytes:
	*
	* 280 % 32 = 24
	*
	* Which can easily be overflowed by adding one more 32 byte entry.
	*/
	almost_full_entries += 8;

	/*if user-specified operated_entries larger than this,decrease to a
	right inlined number
	*/
	if (operated_entries > almost_full_entries)
		operated_entries = almost_full_entries - 1;

	return almost_full_entries;

}
static void get_directory_almost_full(int minus_this_many)
{
	int almost_full_entries;

	almost_full_entries = get_max_inlined_entries(max_inline_size);

	almost_full_entries -= minus_this_many;

	/* Need up to 20 characters to get a 32 byte entry */
	create_files("filename-", almost_full_entries);
}

static void random_unlink(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, *num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		ret = unlink_dirent(dirent);
		if (ret) {
			ret = errno;
			fprintf(stderr, "unlink failure %d: %s\n", ret,
				     strerror(ret));
		} else {

			dirent->name_len = 0;
			iters--;
		}
	}
}

static void random_fill_empty_entries(int iters)
{
	int i, ret, fd;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, *num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len > 0)
			continue;

		dirent->type = S_IFREG >> S_SHIFT;
		dirent->name_len = strlen(dirent->name);
		dirent->seen = 0;

		sprintf(path, "%s/%s", dir_name, dirent->name);

		fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
		if (fd == -1) {
			ret = errno;
			abort_printf("open failure %d: %s\n", ret,
				     strerror(ret));
		}

		close(fd);

		iters--;
	}
}

static void random_rename_same_reclen(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, *num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		strcpy(path, dirent->name);
		path[0] = 'R';
		sprintf(path1, "%s/%s", dir_name, path);
		sprintf(path, "%s/%s", dir_name, dirent->name);

		while (rename(path, path1)) {
			ret = errno;
			fprintf(stderr, "rename failure %d,from %s to %s : "
				"%s\n", ret, path, path1, strerror(ret));
			msync(mmap_shared_dirents_region, mmap_dirents_size,
			      MS_SYNC);
			i = get_rand(0, *num_dirents - 1);
			dirent = &dirents[i];

			if (is_dot_entry(dirent))
				break;
			if (dirent->name_len == 0)
				break;

			strcpy(path, dirent->name);
			path[0] = 'R';
			sprintf(path1, "%s/%s", dir_name, path);
			sprintf(path, "%s/%s", dir_name, dirent->name);
		}

		dirent->name[0] = 'R';
		iters--;
	}
}

static void random_deleting_rename(int iters)
{
	int i, j, ret;
	struct my_dirent *dirent1, *dirent2;

	while (iters--) {
		i = get_rand(0, *num_dirents - 1);
		j = get_rand(0, *num_dirents - 1);
		dirent1 = &dirents[i];
		dirent2 = &dirents[j];

		if (dirent1 == dirent2)
			continue;
		if (is_dot_entry(dirent1) || is_dot_entry(dirent2))
			continue;
		if (dirent1->name_len == 0 || dirent2->name_len == 0)
			continue;

		sprintf(path, "%s/%s", dir_name, dirent1->name);
		sprintf(path1, "%s/%s", dir_name, dirent2->name);

		 while (rename(path, path1)) {
			ret = errno;
			fprintf(stderr, "rename failure %d,from %s to %s: %s\n",
				     ret, path, path1, strerror(ret));
			msync(mmap_shared_dirents_region, mmap_dirents_size,
			      MS_SYNC);
			i = get_rand(0, *num_dirents - 1);
			j = get_rand(0, *num_dirents - 1);
			dirent1 = &dirents[i];
			dirent2 = &dirents[j];

			if (dirent1 == dirent2)
				break;
			if (is_dot_entry(dirent1) || is_dot_entry(dirent2))
				break;
			if (dirent1->name_len == 0 || dirent2->name_len == 0)
				break;

			sprintf(path, "%s/%s", dir_name, dirent1->name);
			sprintf(path1, "%s/%s", dir_name, dirent2->name);
		}

		dirent2->type = dirent1->type;
		dirent1->name_len = 0;
	}
}

static void verify_dirents(void)
{
	int i, ret;
	DIR *dir;
	struct dirent *dirent;
	struct my_dirent *my_dirent;

	sync();

	dir = opendir(dir_name);
	if (dir == NULL) {
		ret = errno;
		abort_printf("opendir failure %d: %s\n", ret, strerror(ret));
	}

	dirent = readdir(dir);
	while (dirent) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			abort_printf("Verify failure: got nonexistent "
				     "dirent: (ino %lu, reclen: %u, type: %u, "
				     "name: %s)\n",
				     dirent->d_ino, dirent->d_reclen,
				     dirent->d_type, dirent->d_name);
		}

		if (my_dirent->type != dirent->d_type) {
			abort_printf("Verify failure: bad dirent type: "
				     "memory: (type: %u, n_len: %u, name: %s), "
				     "kernel: (ino %lu, reclen: %u, type: %u, "
				     "name: %s)\n",
				     my_dirent->type, my_dirent->name_len,
				     my_dirent->name, dirent->d_ino,
				     dirent->d_reclen, dirent->d_type,
				     dirent->d_name);
		}

		if (my_dirent->seen) {
			abort_printf("Verify failure: duplicate dirent: "
				     "(type: %u, name_len: %u, name: %s)\n",
				     my_dirent->type, my_dirent->name_len,
				     my_dirent->name);
		}

		my_dirent->seen++;

		dirent = readdir(dir);
	}

	for (i = 0; i < *num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->seen != 0 || my_dirent->name_len == 0)
			continue;

		abort_printf("Verify failure: missing dirent: "
			     "(type: %u, name_len: %u, name: %s)\n",
			     my_dirent->type, my_dirent->name_len,
			     my_dirent->name);
	}

	/* should reset the 'seen' after verification*/
	for (i = 0; i < *num_dirents; i++) {
		my_dirent = &dirents[i];
		my_dirent->seen = 0;
	}

	closedir(dir);
}

static int is_dir_inlined(char *dirent_name, unsigned long *i_size,
			  unsigned int *id_count)
{
	int ret;
	uint64_t workplace_blk_no = 1;
	uint64_t testdir_blk_no = 1;
	char *buf;
	struct ocfs2_dinode *di;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	sync();

	ocfs2_malloc_block(fs->fs_io, &buf);

	/*lookup worksplace inode*/
	ret = ocfs2_lookup(fs, sb->s_root_blkno, WORK_PLACE,
			   strlen(WORK_PLACE), NULL, &workplace_blk_no);
	if (ret < 0) {
		ocfs2_free(&buf);
		abort_printf("failed to lookup work_place(%s)'s"
			     " inode blkno\n", work_place);
	}

	/*lookup file inode,then read*/
	ret = ocfs2_lookup(fs, workplace_blk_no, dirent_name,
			   strlen(dirent_name), NULL, &testdir_blk_no);
	if (ret < 0) {
		ocfs2_free(&buf);
		abort_printf("failed to lookup file(%s/%s)'s"
			     " inode blkno\n", work_place, dirent_name);
	}

	ret = ocfs2_read_inode(fs, testdir_blk_no, buf);
	if (ret < 0) {
		ocfs2_free(&buf);
		abort_printf("failed to read file(%s/%s/%s)'s"
			     " inode.\n", mount_point, WORK_PLACE, dirent_name);
	}

	di = (struct ocfs2_dinode *)buf;
	*i_size = di->i_size;
	*id_count = ((di->id2).i_data).id_count;

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		ret = 1;
	else
		ret = 0;

	ocfs2_free(&buf);
	return ret;
}

static void should_inlined_or_not(int is_inlined, int should_inlined,
				  int test_no)
{
	/* is_inlined represent if the ret is inlined or not
	   while should_inlined represnt if we expect it inlined or not.
	*/
	if (should_inlined) {
		if (!is_inlined) {
			fprintf(stderr, "After Test #%d, dir %s should be "
				"inlined here!\n", test_no, dir_name);
			fprintf(stderr, "Dir(%s): i_size = %d,id_count = %d\n",
				dir_name, i_size, id_count);
			abort_printf("inline-data check failed!\n");
		}

	} else {
		if (is_inlined) {
			fprintf(stderr, "After Test #%d, dir %s should be "
				"extented here!\n", test_no, dir_name);
			fprintf(stderr, "Dir(%s): i_size = %d,id_count = %d\n",
				dir_name, i_size, id_count);
			abort_printf("inline-data check failed!\n");

		}
	}

	return;
}

static void send_dirents_to_ranks();

static void recv_dirents_from_ranks();
/*
 * [I] Basic tests of inline-dir code.
 *    1) Basic add files
 *    2) Basic delete files
 *    3) Basic rename files
 *    4) Add / Remove / Re-add to fragment dir
 */
static void run_basic_tests(void)
{
	int ret;
	int i;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: fill directory\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(0);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);


	root_printf("Test %d: expand inlined dir to extent exactly\n", testno);
	/*Should be 13 bits length dirent_name*/
	if (rank == 0) {
		create_file("Iam13bitshere");
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		ret = is_dir_inlined(dirent_name, &i_size, &id_count);
		should_inlined_or_not(ret, 0, testno);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		destroy_dir();

	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: rename files with same namelen\n", testno);

	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		random_rename_same_reclen(operated_entries);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: rename files with same namelen on top of each"
		    " other\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		random_deleting_rename(operated_entries);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank ==  0)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: random unlink/fill entries.\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(0);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		random_unlink(operated_entries);
		random_fill_empty_entries(operated_entries);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);

	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);

	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: random rename/unlink files with same namelen\n",
		    testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		random_unlink(operated_entries / 2);
		random_rename_same_reclen(operated_entries / 2);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

/*
 * [II] Tests intended to push a dir out to extents
 *    1) Add enough files to push out one block
 *    2) Add enough files to push out two blocks
 *    3) Fragment dir, add enough files to push out to extents
 */
static void run_large_dir_tests(void)
{
	int ret;

	root_printf("Test %d: Add file name large enough to push out one "
		    "block\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(0);
		create_files("Pushedfn-", 1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);

	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 1) {
		verify_dirents();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	sync();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	printf("ret = %d\n", ret);
	/*verify i_size should be one block size here*/
	if (i_size != blocksize) {
		abort_printf("i_size should be %d,while it's %d here!\n",
			blocksize, i_size);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: Add file name large enough to push out two "
		    "blocks\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(0);
		create_files("this_is_an_intentionally_long_filename_prefix_"
			     "to_stress_the_dir_code-this_is_an_intentionally"
			     "_long_filename_prefix_to_stress_the_dir_code-th"
			     "is_is_an_intentionally_long_filename_prefix_to_"
			     "stress_the_dir_code", 1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 1)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	/*verify i_size should be one block size here*/
	if (i_size != blocksize * 2) {
		fprintf(stderr, "i_size should be %d,while it's %d here!\n",
			blocksize * 2, i_size);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test %d: fragment directory then push out to extents.\n",
	       testno);
	if (rank == 0) {
		create_and_prep_dir();
		get_directory_almost_full(1);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		random_unlink(operated_entries / 2);
		random_rename_same_reclen(operated_entries / 2);
		create_files("frag2a", operated_entries / 2);
		random_unlink(operated_entries / 2);
		create_files("frag2b", operated_entries / 2);
		random_deleting_rename(operated_entries / 2);
		random_rename_same_reclen(operated_entries / 2);
		create_files("frag2c", operated_entries / 2 + 1);
		create_files("frag2d", operated_entries);
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);

	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0) {
		destroy_dir();
		msync(mmap_shared_dirents_region, mmap_dirents_size,
		      MS_SYNC | MS_INVALIDATE);
		msync(mmap_shared_num_region, mmap_num_size,
		      MS_SYNC | MS_INVALIDATE);
	}
	testno++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

static int uuid2dev(const char *uuid, char *dev)
{
	FILE *df;
	char cmd[300];

	snprintf(cmd, 300, "blkid |grep %s|cut -d':' -f1", uuid);

	df = popen(cmd, "r");
	if (df == NULL) {
		fprintf(stderr, "popen failed to get dev name.\n");
		return -1;
	}

	fscanf(df, "%s\n", dev);

	pclose(df);
	return 0;
}

static int open_ocfs2_volume(char *device_name)
{
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;
	int ret;

	ret = ocfs2_open(device_name, open_flags, 0, 0, &fs);
	if (ret < 0)
		abort_printf("Not a ocfs2 volume!\n");

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
	if (!(ocfs2_sb->s_feature_incompat &
	      OCFS2_FEATURE_INCOMPAT_INLINE_DATA))
		abort_printf("Inline-data not supported on this volume\n");

	blocksize = 1 << ocfs2_sb->s_blocksize_bits;
	clustersize = 1 << ocfs2_sb->s_clustersize_bits;
	max_inline_size = ocfs2_max_inline_data(blocksize);

	return 0;
}

static void setup_mmap_sharing(void)
{
	int ret;
	int i;
	unsigned long total_size = sizeof(struct my_dirent) * MAX_DIRENTS;

	mmap_num_size = page_size;
	mmap_dirents_size = page_size;

	while (mmap_dirents_size < total_size)
		mmap_dirents_size += page_size;

	snprintf(shared_mmap_dirents_file, OCFS2_MAX_FILENAME_LEN, "%s/%s",
		 work_place, SHARED_MMAP_DIRENTS_FILE);
	snprintf(shared_mmap_num_file, OCFS2_MAX_FILENAME_LEN, "%s/%s",
		 work_place, SHARED_MMAP_NUM_FILE);

	if (rank == 0) {
		mmap_dirents_fd = open(shared_mmap_dirents_file,
				       O_CREAT | O_RDWR | O_TRUNC | O_APPEND,
				       FILE_MODE);
		mmap_num_fd = open(shared_mmap_num_file,
				   O_CREAT | O_RDWR | O_TRUNC | O_APPEND,
				   FILE_MODE);

		if (mmap_dirents_fd < 0 || mmap_num_fd < 0)
			abort_printf("create mmap shared file failed!\n");

		ftruncate(mmap_dirents_fd, mmap_dirents_size);
		fsync(mmap_dirents_fd);

		ftruncate(mmap_num_fd, mmap_num_size);
		fsync(mmap_num_fd);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		mmap_dirents_fd = open(shared_mmap_dirents_file,
				       O_RDWR | O_SYNC, FILE_MODE);
		mmap_num_fd = open(shared_mmap_num_file, O_RDWR | O_SYNC,
				   FILE_MODE);
		if (mmap_dirents_fd < 0 || mmap_num_fd < 0)
			abort_printf("open mmap shared file failed!\n");
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	mmap_shared_dirents_region = mmap(NULL, mmap_dirents_size, PROT_WRITE,
					  MAP_SHARED, mmap_dirents_fd, 0);

	mmap_shared_num_region = mmap(NULL, mmap_num_size, PROT_WRITE,
				      MAP_SHARED, mmap_num_fd, 0);

	if (mmap_shared_dirents_region == MAP_FAILED ||
	    mmap_shared_num_region == MAP_FAILED) {
		ret = errno;
		abort_printf("mmap error %d: \"%s\"\n", ret, strerror(ret));
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

}

static void setup(int argc, char *argv[])
{
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Init Failed!\n");

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0)
		abort_printf("get hostname!\n");

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);


	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	memset(device, 0, 100);

	if (uuid2dev(uuid, device))
		abort_printf("Failed to get device name from uuid!\n");

	ret = open_ocfs2_volume(device);
	if (ret < 0)
		abort_printf("open ocfs2 volume failed!\n");

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);
	snprintf(work_place, OCFS2_MAX_FILENAME_LEN, "%s/%s", mount_point,
		 WORK_PLACE);

	snprintf(dirent_name, OCFS2_MAX_FILENAME_LEN,
		 "multiple-inline-data-dir-test");
	snprintf(dir_name, OCFS2_MAX_FILENAME_LEN, "%s/%s", work_place,
		 dirent_name);

	if (rank == 0) {
		mkdir(work_place, FILE_MODE);
		root_printf("BlockSize:\t\t%d\nMax Inline Data Size:\t%d\n"
		       "ClusterSize:\t\t%d\nPageSize:\t\t%d\nWorkingPlace:"
		       "\t\t%s\nNumOfMaxInlinedEntries:\t\t%d\n\n", blocksize,
		       max_inline_size, clustersize, page_size, work_place,
		       get_max_inlined_entries(max_inline_size));
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	setup_mmap_sharing();

	num_dirents = (unsigned int *)mmap_shared_num_region;

	dirents = (struct my_dirent *)mmap_shared_dirents_region;

	if (rank == 0) {
		memset(mmap_shared_dirents_region, 0, mmap_dirents_size);
		fsync(mmap_dirents_fd);
		memset(mmap_shared_num_region, 0, mmap_num_size);
		fsync(mmap_num_fd);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	return;
}

static int teardown(void)
{
	if (mmap_shared_dirents_region)
		munmap(mmap_shared_dirents_region, mmap_dirents_size);

	if (mmap_shared_num_region)
		munmap(mmap_shared_num_region, mmap_num_size);

	if (rank == 0) {
		close(mmap_dirents_fd);
		close(mmap_num_fd);
		unlink(shared_mmap_dirents_file);
		unlink(shared_mmap_num_file);
		rmdir(work_place);
	}

	MPI_Finalize();

	return 0;
}

static void send_dirents_to_ranks(void)
{
	int i, ret;

	MPI_Request request;
	MPI_Status status;

	if (rank == 0) {
		for (i = 1; i < size; i++) {
			ret = MPI_Isend(dirents,
					sizeof(struct my_dirent) * MAX_DIRENTS,
					MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Isend dirents failed!\n");
			MPI_Wait(&request, &status);

			ret = MPI_Isend(num_dirents, sizeof(unsigned int),
					MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Isend num_dirents failed!\n");
			MPI_Wait(&request, &status);
		}

	} else {
		ret = MPI_Isend(dirents,
				sizeof(struct my_dirent) * MAX_DIRENTS,
				MPI_BYTE, 0, 1, MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Isend dirents failed!\n");
		MPI_Wait(&request, &status);

		ret = MPI_Isend(num_dirents, sizeof(unsigned int), MPI_BYTE,
				i, 1, MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Isend num_dirents failed!\n");
		MPI_Wait(&request, &status);
	}
}

static void recv_dirents_from_ranks(void)
{
	int i, ret;

	MPI_Request request;
	MPI_Status status;

	if (rank == 0) {
		for (i = 1; i < size; i++) {
			ret = MPI_Irecv(dirents,
					sizeof(struct my_dirent) * MAX_DIRENTS,
					MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);

			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Irecv dirents failed!\n");
			MPI_Wait(&request, &status);

			ret = MPI_Irecv(num_dirents, sizeof(unsigned int),
					MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Irecv num_dirents failed!\n");
			MPI_Wait(&request, &status);
		}

	} else {
		ret = MPI_Irecv(dirents,
				sizeof(struct my_dirent) * MAX_DIRENTS,
				MPI_BYTE, 0, 1, MPI_COMM_WORLD, &request);

		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Irecv dirents failed!\n");
		MPI_Wait(&request, &status);

		ret = MPI_Irecv(num_dirents, sizeof(unsigned int), MPI_BYTE,
				0, 1, MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Irecv num_dirents failed!\n");
		MPI_Wait(&request, &status);
	}

}

int main(int argc, char **argv)
{
	int i, ret;

	setup(argc, argv);

	for (i = 0; i < iteration; i++) {

		root_printf("################Test Round %d################\n",
			    i);
		testno = 1;
		run_basic_tests();
		root_printf("All File I/O Tests Passed\n");

		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Barrier failed: %d\n", ret);

	}

	teardown();

	return 0;
}
