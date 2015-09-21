/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inline-dirs-utils.c
 *
 * All utility functions used by both single and multiple
 * nodes inline-dirs testing.
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

#include <inttypes.h>
#include <linux/types.h>

#define OCFS2_MAX_FILENAME_LEN	  	255
#define WORK_PLACE			"inline-data-test"
#define MAX_DIRENTS		     1024

#define FILE_BUFFERED_RW_FLAGS	(O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE		(S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

struct my_dirent {
	unsigned int    type;
	unsigned int    name_len;
	unsigned int    seen;
	char	    name[OCFS2_MAX_FILENAME_LEN];
};

extern ocfs2_filesys *fs;
extern struct ocfs2_super_block *ocfs2_sb;

extern unsigned int blocksize;
extern unsigned long clustersize;
extern unsigned int max_inline_size;

extern unsigned int id_count;
extern unsigned long i_size;

extern char mount_point[OCFS2_MAX_FILENAME_LEN];
extern char work_place[OCFS2_MAX_FILENAME_LEN];
extern char dirent_name[OCFS2_MAX_FILENAME_LEN];
extern char dir_name[OCFS2_MAX_FILENAME_LEN];

extern unsigned int operated_entries;

extern struct my_dirent *dirents;
extern unsigned int num_dirents;

extern char path[PATH_MAX];
extern char path1[PATH_MAX];

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

	for (i = 0; i < num_dirents; i++) {
		dirent = &dirents[i];

		if (dirent->name_len == 0)
			continue;

		if (!is_dot_entry(dirent)) {
			ret = unlink_dirent(dirent);
			if (ret) {
				ret = errno;
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
				exit(ret);
			}
		}

		dirent->name_len = 0;
	}

	ret = rmdir(dir_name);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		exit(ret);
	}
}

struct my_dirent *find_my_dirent(char *name)
{
	int i, len;
	struct my_dirent *my_dirent;

	len = strlen(name);

	for (i = 0; i < num_dirents; i++) {
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

	num_dirents = 2;
	ret = mkdir(dir_name, FILE_MODE);
	if (ret) {
		ret = errno;
		fprintf(stderr, "mkdir failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}
}

void create_file(char *filename)
{
	int ret, fd;
	struct my_dirent *dirent;

	dirent = &dirents[num_dirents];
	num_dirents++;

	dirent->type = S_IFREG >> S_SHIFT;
	dirent->name_len = strlen(filename);
	dirent->seen = 0;
	strcpy(dirent->name, filename);

	sprintf(path, "%s/%s", dir_name, dirent->name);

	fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "open failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}

	close(fd);
}

void create_files(char *prefix, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		sprintf(path1, "%s%011d", prefix, i);
		create_file(path1);
	}
}

int get_max_inlined_entries(int max_inline_size)
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

void get_directory_almost_full(int minus_this_many)
{
	int almost_full_entries;

	almost_full_entries = get_max_inlined_entries(max_inline_size);

	almost_full_entries -= minus_this_many;

	/* Need up to 20 characters to get a 32 byte entry */
	create_files("filename-", almost_full_entries);
}

void random_unlink(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, num_dirents - 1);
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
			exit(ret);
		}

		dirent->name_len = 0;
		iters--;
	}
}

void random_fill_empty_entries(int iters)
{
	int i, ret, fd;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, num_dirents - 1);
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
			fprintf(stderr, "open failure %d: %s\n", ret,
				strerror(ret));
			exit(ret);
		}

		close(fd);

		iters--;
	}
}

void random_rename_same_reclen(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters > 0) {
		i = get_rand(0, num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		/*
		 * We already renamed this one
		 */
		/*
		if (dirent->name[0] == 'R')
			continue;
		*/

		strcpy(path, dirent->name);
		path[0] = 'R';
		sprintf(path1, "%s/%s", dir_name, path);
		sprintf(path, "%s/%s", dir_name, dirent->name);

		ret = rename(path, path1);
		if (ret) {
			ret = errno;
			fprintf(stderr, "rename failure %d: %s\n", ret,
				strerror(ret));

			fprintf(stderr, "Failed rename from %s to %s\n",
				path, path1);

			exit(ret);
		}
		dirent->name[0] = 'R';
		iters--;
	}
}

void random_deleting_rename(int iters)
{
	int i, j, ret;
	struct my_dirent *dirent1, *dirent2;

	while (iters--) {
		i = get_rand(0, num_dirents - 1);
		j = get_rand(0, num_dirents - 1);
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

		ret = rename(path, path1);
		if (ret) {
			ret = errno;
			fprintf(stderr, "rename failure %d: %s\n", ret,
				strerror(ret));

			fprintf(stderr, "Failed rename from %s to %s\n",
				path, path1);

			exit(ret);
		}
		dirent2->type = dirent1->type;
		dirent1->name_len = 0;
	}
}

void verify_dirents(void)
{
	int i, ret;
	DIR *dir;
	struct dirent *dirent;
	struct my_dirent *my_dirent;

	dir = opendir(dir_name);
	if (dir == NULL) {
		ret = errno;
		fprintf(stderr, "opendir failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}

	dirent = readdir(dir);
	while (dirent) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			fprintf(stderr, "Verify failure: got nonexistent "
				"dirent: (ino %lu, reclen: %u, type: %u, "
				"name: %s)\n",
				dirent->d_ino, dirent->d_reclen,
				dirent->d_type, dirent->d_name);
			exit(1);
		}

		if (my_dirent->type != dirent->d_type) {
			fprintf(stderr, "Verify failure: bad dirent type: "
				"memory: (type: %u, name_len: %u, name: %s), "
				"kernel: (ino %lu, reclen: %u, type: %u, "
				"name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name, dirent->d_ino,
				dirent->d_reclen, dirent->d_type,
				dirent->d_name);
			exit(1);
		}

		if (my_dirent->seen) {
			fprintf(stderr, "Verify failure: duplicate dirent: "
				"(type: %u, name_len: %u, name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name);
			exit(1);
		}

		my_dirent->seen++;

		dirent = readdir(dir);
	}

	for (i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->seen != 0 || my_dirent->name_len == 0)
			continue;

		fprintf(stderr, "Verify failure: missing dirent: "
			"(type: %u, name_len: %u, name: %s)\n", my_dirent->type,
			my_dirent->name_len, my_dirent->name);
		exit(1);
	}
	closedir(dir);
}

int open_ocfs2_volume(char *device_name)
{
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;
	int ret;

	ret = ocfs2_open(device_name, open_flags, 0, 0, &fs);
	if (ret < 0) {
		fprintf(stderr, "Not a ocfs2 volume!\n");
		return ret;
	}

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
	if (!(ocfs2_sb->s_feature_incompat &
	      OCFS2_FEATURE_INCOMPAT_INLINE_DATA)) {
		fprintf(stderr, "Inline-data not supported"
			" on this ocfs2 volume\n");
		return -1;
	}

	blocksize = 1 << ocfs2_sb->s_blocksize_bits;
	clustersize = 1 << ocfs2_sb->s_clustersize_bits;
	max_inline_size = ocfs2_max_inline_data_with_xattr(blocksize, NULL);

	return 0;
}

int is_dir_inlined(char *dirent_name, unsigned long *i_size,
			   unsigned int *id_count)
{
	int ret;
	uint64_t workplace_blk_no = 1;
	uint64_t testdir_blk_no = 1;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	sync();

	ocfs2_malloc_block(fs->fs_io, &buf);

	/*lookup worksplace inode*/
	ret = ocfs2_lookup(fs, sb->s_root_blkno, WORK_PLACE,
			   strlen(WORK_PLACE), NULL, &workplace_blk_no);
	if (ret < 0) {
		fprintf(stderr, "failed to lookup work_place(%s)'s"
			" inode blkno\n", work_place);
		ocfs2_free(&buf);
		exit(ret);
	}

	/*lookup file inode,then read*/
	ret = ocfs2_lookup(fs, workplace_blk_no, dirent_name,
			   strlen(dirent_name), NULL, &testdir_blk_no);
	if (ret < 0) {
		fprintf(stderr, "failed to lookup file(%s/%s)'s"
			" inode blkno\n", work_place, dirent_name);
		ocfs2_free(&buf);
		exit(ret);
	}

	ret = ocfs2_read_inode(fs, testdir_blk_no, buf);
	if (ret < 0) {
		fprintf(stderr, "failed to read file(%s/%s/%s)'s"
			" inode.\n", mount_point, WORK_PLACE, dirent_name);
		ocfs2_free(&buf);
		exit(ret);
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

void should_inlined_or_not(int is_inlined, int should_inlined, int test_no)
{
	/* is_inlined represent if the ret is inlined or not
	   while should_inlined represnt if we expect it inlined or not.
	*/
	if (should_inlined) {
		if (!is_inlined) {
			fprintf(stderr, "After Test #%d, dir %s should be "
				"inlined here!\n", test_no, dir_name);
			fprintf(stderr, "Dir(%s): i_size = %lu, id_count = %d\n",
				dir_name, i_size, id_count);
			exit(-1);
		}

	} else {
		if (is_inlined) {
			fprintf(stderr, "After Test #%d, dir %s should be "
				"extented here!\n", test_no, dir_name);
			fprintf(stderr, "Dir(%s): i_size = %lu, id_count = %d\n",
				dir_name, i_size, id_count);
			exit(-1);

		}
	}

	return;
}


