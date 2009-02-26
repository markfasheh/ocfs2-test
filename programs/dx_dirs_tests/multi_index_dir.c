/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi_index_dir.c
 *
 * A mpi compatible program to test indexed-dirs
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
#define MAX_DIRENTS			20000

#define FILE_BUFFERED_RW_FLAGS  (O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define SHARED_MMAP_DIRENTS_FILE        ".shared_mmap_dirents_file"
#define SHARED_MMAP_NUM_FILE            ".shared_mmap_num_file"

#define GROW_TEST			0x00000001
#define RENM_TEST			0x00000002
#define READ_TEST			0x00000004
#define STRS_TEST			0x00000008
#define ULNK_TEST			0x00000010
#define FLUP_TEST			0x00000020

struct my_dirent {
	unsigned int	type;
	unsigned int	name_len;
	unsigned int	seen;
	char		name[OCFS2_MAX_FILENAME_LEN];
};

char *prog;

/*
ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;
*/

unsigned long page_size;

unsigned int id_count;
unsigned long i_size;

char work_place[OCFS2_MAX_FILENAME_LEN];
char dirent_name[OCFS2_MAX_FILENAME_LEN];
char dir_name[OCFS2_MAX_FILENAME_LEN];
char shared_mmap_dirents_file[OCFS2_MAX_FILENAME_LEN];
char shared_mmap_num_file[OCFS2_MAX_FILENAME_LEN];
char *mmap_shared_dirents_region;
char *mmap_shared_num_region;
unsigned long mmap_dirents_size;
unsigned long mmap_num_size;
int mmap_dirents_fd;
int mmap_num_fd;

int iteration = 1;
unsigned int operated_entries = 20;

struct my_dirent *dirents;
unsigned long *num_dirents;

char path[PATH_MAX];
char path1[PATH_MAX];

int testno = 1;

int rank = -1, size;
char hostname[HOSTNAME_MAX_SZ];

int test_flags = 0x00000000;

unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

char rand_char(void)
{
	return 'A' + (char) get_rand(0, 25);
}

unsigned int get_rand_nam(char *str, unsigned int least, unsigned most)
{
	unsigned nam_len = get_rand(least, most);
	unsigned i = 0;

	memset(str, 0, OCFS2_MAX_FILENAME_LEN);

	while (i < nam_len) {
		str[i] = rand_char();
		i++;
	}

	str[nam_len] = '\0';
	return nam_len;
}

void usage(void)
{
	printf("Usage: multi_index_dir [-i <iteration>] [-n operated_entries] "
	       "<-w workplace> [-s] [-g] [-r] [-m] [-u] [-f]\n"
	       "Run a series of tests intended to verify I/O to and from "
	       "indexed dirs\n"
	       "iteration specify the running times.\n"
	       "operated_dir_entries specify the entires number to be operated"
	       "-s specify the stress test"
	       "-g specify the grow test"
	       "-r specify the read test"
	       "-m specify the rename test"
	       "-u specify the unlink test"
	       "-f specify the fillup test"
	       "workplace is mandatory.\n");

	MPI_Finalize();
	exit(1);
}

void abort_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

void root_printf(const char *fmt, ...)
{
	va_list ap;

	if (rank == 0) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
	}
}

int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "I:i:w:W:N:sSgGrRmMuUfFn:");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
		case 'I':
			iteration = atol(optarg);
			break;
		case 'N':
		case 'n':
			operated_entries = atol(optarg);
			break;
		case 'w':
		case 'W':
			strcpy(work_place, optarg);
			break;
		case 's':
		case 'S':
			test_flags = STRS_TEST;
			break;
		case 'g':
		case 'G':
			test_flags |= GROW_TEST;
			break;
		case 'r':
		case 'R':
			test_flags |= READ_TEST;
			break;
		case 'm':
		case 'M':
			test_flags |= RENM_TEST;
			break;
		case 'u':
		case 'U':
			test_flags |= ULNK_TEST;
			break;
		case 'f':
		case 'F':
			test_flags |= FLUP_TEST;
			break;
		default:
			break;
		}
	}

	if (strcmp(work_place, "") == 0)
		return EINVAL;

	if (operated_entries > MAX_DIRENTS)
		test_flags = STRS_TEST;

	return 0;
}

int is_dot_entry(struct my_dirent *dirent)
{
	if (dirent->name_len == 1 && dirent->name[0] == '.')
		return 1;
	if (dirent->name_len == 2 && dirent->name[0] == '.'
	    && dirent->name[1] == '.')
		return 1;
	return 0;
}

int unlink_dirent(struct my_dirent *dirent)
{
	sprintf(path1, "%s/%s", dir_name, dirent->name);

	return unlink(path1);
}

void destroy_dir(void)
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
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
			}
		}

		dirent->name_len = 0;
	}

	ret = rmdir(dir_name);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			     strerror(ret));
	}
	
	*num_dirents = 0;
}

struct my_dirent *find_my_dirent(char *name)
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

void create_and_prep_dir(void)
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

void create_file(char *filename)
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

void create_files(char *prefix, int num)
{
	int i;
	char dirent_nam[OCFS2_MAX_FILENAME_LEN];

	for (i = 0; i < num; i++) {
		if (!prefix)
			get_rand_nam(dirent_nam, 1, OCFS2_MAX_FILENAME_LEN);
		else
			sprintf(dirent_nam, "%s-%s-%d-%d", prefix, hostname,
				rank, i);

		create_file(dirent_nam);
	}
}

void random_read(int iters)
{
	DIR *dir;
	int i, ret;
	struct my_dirent *my_dirent;
	struct dirent *dirent;
	struct stat stat_info;
	char fullpath[PATH_MAX];

	dir = opendir(dir_name);
        if (dir == NULL) {
                ret = errno;
                abort_printf("opendir failure %d: %s\n", ret, strerror(ret));
        }


	while (dirent = readdir(dir)) {
		snprintf(fullpath, PATH_MAX, "%s/%s", dir_name, dirent->d_name);
		ret = stat(fullpath, &stat_info);
		if (ret) {
			ret = errno;
			abort_printf("stat failure %d: %s\n", ret,
				     strerror(ret));
		}
	}

	while (iters > 0) {
		i = get_rand(0, *num_dirents - 1);
                my_dirent = &dirents[i];
		sprintf(fullpath, "%s/%s", dir_name, my_dirent->name);
		ret = stat(fullpath, &stat_info);
		if (ret) {
			ret = errno;
			abort_printf("stat failure %d: %s\n", ret,
				     strerror(ret));
                }

		iters--;
	}
}

void random_unlink(int iters)
{
	int i, ret;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0;

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		times++;
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
		}

		dirent->name_len = 0;

		iters--;
	}
}

void random_fill_empty_entries(int iters)
{
	int i, ret, fd;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0;

	threshold = iters * 2;	

	while ((iters > 0) && (times < threshold)) {
		times++;
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

void random_rename_same_reclen(int iters)
{
	int i, ret;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0;

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		times++;
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

void random_deleting_rename(int iters)
{
	int i, j, ret;
	struct my_dirent *dirent1, *dirent2;
	unsigned long threshold, times = 0;

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		times++;
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

		iters--;
	}
}

void verify_dirents(void)
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

	while (dirent = readdir(dir)) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			root_printf("Verify failure: got nonexistent "
				     "dirent: (ino %lu, reclen: %u, type: %u, "
				     "name: %s)\n",
				     dirent->d_ino, dirent->d_reclen,
				     dirent->d_type, dirent->d_name);
			continue;
		}

		if (my_dirent->type != dirent->d_type) {
			root_printf("Verify failure: bad dirent type: "
				     "memory: (type: %u, n_len: %u, name: %s), "
				     "kernel: (ino %lu, reclen: %u, type: %u, "
				     "name: %s)\n",
				     my_dirent->type, my_dirent->name_len,
				     my_dirent->name, dirent->d_ino,
				     dirent->d_reclen, dirent->d_type,
				     dirent->d_name);
		}

		if (my_dirent->seen) {
			root_printf("Verify failure: duplicate dirent: "
				     "(type: %u, name_len: %u, name: %s)\n",
				     my_dirent->type, my_dirent->name_len,
				     my_dirent->name);
		}

		my_dirent->seen++;
	}

	for (i = 0; i < *num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->seen != 0 || my_dirent->name_len == 0)
			continue;

		root_printf("Verify failure: missing dirent: "
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

static void MPI_Barrier_Sync(void)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

void sync_mmap(void)
{
	msync(mmap_shared_dirents_region, mmap_dirents_size, MS_SYNC);
	msync(mmap_shared_num_region, mmap_num_size, MS_SYNC);
}

void grow_test()
{
	MPI_Barrier_Sync();

	root_printf("Test %d: Growing tests\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		sync_mmap();
	}
	
	MPI_Barrier_Sync();

	if (rank) {
		create_files("filename", operated_entries);
		sync_mmap();
	}
	
	MPI_Barrier_Sync();

	if (rank == 0) {
		verify_dirents();
	}

	sleep(2);
	
	MPI_Barrier_Sync();


	testno++;
}

void rw_test()
{
	MPI_Barrier_Sync();

	root_printf("Test %d: Concurrent rename test\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		create_files("rwtestfile", operated_entries);
		sync_mmap();
	}

	MPI_Barrier_Sync();

	if (rank) {
		random_rename_same_reclen(operated_entries / 2);
		sync_mmap();
	}
	
	MPI_Barrier_Sync();

	sleep(2);

	if (rank == 0) {
                verify_dirents();
        }

        sleep(2);

	MPI_Barrier_Sync();

        if (rank == 0)
                destroy_dir();

	testno++;
}

void read_test()
{
	MPI_Barrier_Sync();

	root_printf("Test %d: Random concurrent read test\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		create_files(NULL, operated_entries);
		sync_mmap();
	}

	MPI_Barrier_Sync();

	if (rank) {
		random_read(operated_entries / 2);
	}
	
	MPI_Barrier_Sync();

	sleep(2);

	if (rank == 0) {
                verify_dirents();
        }

	MPI_Barrier_Sync();

	if (rank == 0)
		destroy_dir();

	testno++;

}

void unlink_test()
{
	MPI_Barrier_Sync();

	root_printf("Test %d: Random concurrent unlink test\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		create_files("uktestfile", operated_entries);
		sync_mmap();
	}

	MPI_Barrier_Sync();

	if (rank) {
		random_unlink(operated_entries / 2);
		sync_mmap();
	}
	
	MPI_Barrier_Sync();

	sleep(2);

	if (rank == 0) {
		verify_dirents();
	}

	MPI_Barrier_Sync();

	if (rank == 0)
		destroy_dir();

	testno++;
}

void fillup_test()
{
	MPI_Barrier_Sync();

	root_printf("Test %d: Random fillup test\n", testno);
	if (rank == 0) {
		create_and_prep_dir();
		create_files("fptestfile", operated_entries);
		sync_mmap();
		random_unlink(operated_entries);
		sync_mmap();
	}

	MPI_Barrier_Sync();

	if (rank) {
		random_fill_empty_entries(operated_entries / size);
		sync_mmap();
	}
	
	MPI_Barrier_Sync();

	sleep(2);

	if (rank == 0) {
		verify_dirents();
	}

	MPI_Barrier_Sync();

	if (rank == 0)
		destroy_dir();

	testno++;
}

void stress_test()
{
	unsigned long i;
	int ret, fd;
	char dirent_nam[OCFS2_MAX_FILENAME_LEN];

	MPI_Barrier_Sync();

	root_printf("Test %d: Stress test\n", testno);
	if (rank == 0) {
		ret = mkdir(dir_name, FILE_MODE);
	        if (ret) {
			ret = errno;
			abort_printf("mkdir failure %d: %s\n", ret, strerror(ret));
		}

	}

	MPI_Barrier_Sync();

	for (i = 0; i < operated_entries; i++) {
		get_rand_nam(dirent_nam, 1, OCFS2_MAX_FILENAME_LEN-4);
		sprintf(path, "%s/%s-%d", dir_name, dirent_nam, rank);
		fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			abort_printf("open failure %d: %s\n", ret, strerror(ret));
		}
		close(fd);
	}
	
	MPI_Barrier_Sync();

	testno++;
}

void run_tests(int iter)
{
	testno = 1;
	snprintf(dirent_name, OCFS2_MAX_FILENAME_LEN,
                 "multi-indexed-dirs-test-%d", iter);
        snprintf(dir_name, OCFS2_MAX_FILENAME_LEN, "%s/%s", work_place,
                 dirent_name);

	if (test_flags & GROW_TEST)
		grow_test();
	
	if (test_flags & RENM_TEST)
		rw_test();

	if (test_flags & READ_TEST)
		read_test();

	if (test_flags & ULNK_TEST)
		unlink_test();

	if (test_flags & FLUP_TEST)
		fillup_test();

	if (test_flags & STRS_TEST)
		stress_test();
}

void setup_mmap_sharing(void)
{
	int ret;
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

	MPI_Barrier_Sync();

	if (rank != 0) {
		mmap_dirents_fd = open(shared_mmap_dirents_file,
				       O_RDWR | O_SYNC, FILE_MODE);
		mmap_num_fd = open(shared_mmap_num_file, O_RDWR | O_SYNC,
				   FILE_MODE);
		if (mmap_dirents_fd < 0 || mmap_num_fd < 0)
			abort_printf("open mmap shared file failed!\n");
	}

	MPI_Barrier_Sync();

	mmap_shared_dirents_region = mmap(NULL, mmap_dirents_size, PROT_WRITE,
					  MAP_SHARED, mmap_dirents_fd, 0);

	mmap_shared_num_region = mmap(NULL, mmap_num_size, PROT_WRITE,
				      MAP_SHARED, mmap_num_fd, 0);

	if (mmap_shared_dirents_region == MAP_FAILED ||
	    mmap_shared_num_region == MAP_FAILED) {
		ret = errno;
		abort_printf("mmap error %d: \"%s\"\n", ret, strerror(ret));
	}

	MPI_Barrier_Sync();

}

void setup(int argc, char *argv[])
{
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Init failed!\n");

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0)
		abort_printf("Get hostname failed!\n");

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

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);

	MPI_Barrier_Sync();

	setup_mmap_sharing();

	num_dirents = (unsigned long *)mmap_shared_num_region;

	dirents = (struct my_dirent *)mmap_shared_dirents_region;

	if (rank == 0) {
		memset(mmap_shared_dirents_region, 0, mmap_dirents_size);
		fsync(mmap_dirents_fd);
		memset(mmap_shared_num_region, 0, mmap_num_size);
		fsync(mmap_num_fd);
	}

	MPI_Barrier_Sync();

	return;
}

int teardown(void)
{
	if (mmap_shared_dirents_region)
		munmap(mmap_shared_dirents_region, mmap_dirents_size);

	if (mmap_shared_num_region)
		munmap(mmap_shared_num_region, mmap_num_size);

	if (rank == 0) {
		close(mmap_dirents_fd);
		close(mmap_num_fd);
	}

	MPI_Finalize();

	return 0;
}

int main(int argc, char **argv)
{
	int i;

	setup(argc, argv);

	for (i = 0; i < iteration; i++) {

		root_printf("################Test Round %d################\n",
			    i);
		testno = 1;
		run_tests(i);
		root_printf("All File I/O Tests Passed\n");
		MPI_Barrier_Sync();
	}

	teardown();
	
	return 0;
}
