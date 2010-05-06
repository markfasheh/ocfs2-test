/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inline-data-utils.c
 *
 * All utility functions used by both single and multiple
 * nodes inline-data testing.
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

#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <ocfs2/ocfs2.h>

#include <sys/ioctl.h>
#include <inttypes.h>
#include <linux/types.h>

#define PATTERN_SZ      	8192
#define OCFS2_MAX_FILENAME_LEN	255

#define FILE_BUFFERED_RW_FLAGS  (O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define WORK_PLACE      "inline-data-test"

extern ocfs2_filesys *fs;
extern struct ocfs2_super_block *ocfs2_sb;

extern char *pattern;
extern unsigned int max_inline_size;
extern char *read_buf;

extern unsigned long page_size;
extern unsigned int blocksize;
extern unsigned long clustersize;

extern unsigned int id_count;
extern unsigned long i_size;

extern char mount_point[OCFS2_MAX_FILENAME_LEN];
extern char work_place[OCFS2_MAX_FILENAME_LEN];
extern char file_name[OCFS2_MAX_FILENAME_LEN];

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

void fill_pattern(int size)
{
	int i;

	/*
	* Make sure that anything in the buffer past size is zero'd,
	* as a regular file should look.
	*/
	memset(pattern, 0, PATTERN_SZ);
	for (i = 0; i < size; i++)
		pattern[i] = rand_char();
}

int truncate_pattern(int fd, unsigned int old_size, unsigned int new_size)
{
	int bytes = old_size - new_size;
	int ret;

	memset(pattern + new_size, 0, bytes);

	ret = ftruncate(fd, new_size);
	if (ret == -1) {
		fprintf(stderr, "ftruncate error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

int extend_pattern(int fd, unsigned int old_size, unsigned int new_size)
{
	int bytes = new_size - old_size;
	int ret;
	int i;

	memset(pattern + old_size, 0, bytes);

	ret = ftruncate(fd, new_size);
	if (ret == -1) {
		fprintf(stderr, "ftruncate error %d: \"%s\"\n",
		ret, strerror(ret));
		return -1;
	}

	return 0;
}

int try_ioctl(int which, int fd, unsigned int offset, unsigned int count)
{
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = SEEK_SET;
	sr.l_start = offset;
	sr.l_len = count;

	return ioctl(fd, which, &sr);
}

int try_reserve(int fd, unsigned int offset, unsigned int count)
{
	int ret;

	ret = try_ioctl(OCFS2_IOC_RESVSP, fd, offset, count);
	if (ret == -1) {
		ret = errno;
		if (ret == ENOTTY)
			return ret;
		fprintf(stderr, "IOC_RESVSP error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

int try_punch_hole(int fd, unsigned int offset, unsigned int count)
{
	int ret;

	memset(pattern + offset, 0, count);

	ret = try_ioctl(OCFS2_IOC_UNRESVSP, fd, offset, count);
	if (ret == -1) {
		ret = errno;
		if (ret == ENOTTY)
			return ret;
		fprintf(stderr, "IOC_UNRESVSP error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

int mmap_write_at(int fd, const char *buf, size_t count, off_t offset)
{
	unsigned int mmap_size = page_size;
	unsigned int size = count + offset;
	int i, j, ret;
	char *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (write) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	j = 0;
	for (i = offset; i < size; i++)
		region[i] = buf[j++];

	munmap(region, mmap_size);

	return 0;
}

int write_at(int fd, const void *buf, size_t count, off_t offset)
{
	int ret;

	ret = pwrite(fd, buf, count, offset);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "write error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	if (ret != count) {
		fprintf(stderr, "Short write: wanted %d, got %d\n", count, ret);
		return -1;
	}

	return 0;
}

int prep_file_no_fill(unsigned int size, int open_direct)
{
	int fd, ret;
	int flags = FILE_BUFFERED_RW_FLAGS;
	size_t count = size;

	if (open_direct) {
		flags |= O_DIRECT;
		count = blocksize;
	}

	fd = open(file_name, flags, FILE_MODE);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "open error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	ret = write_at(fd, pattern, count, 0);
	if (ret)
		return ret;

	if (count > size) {
		ret = ftruncate(fd, size);
		if (ret) {
			ret = errno;
			fprintf(stderr, "truncate error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}
	}

	return fd;
}

int prep_file(unsigned int size)
{
	fill_pattern(size);

	return prep_file_no_fill(size, 0);
}

int verify_pattern(int size, char *buf)
{
	int i;

	for (i = 0; i < size; i++) {
		if (buf[i] != pattern[i]) {
			fprintf(stderr, "Verify failed at byte: %d\n", i);
			return -1;
		}
	}
	return 0;
}

int __verify_pattern_fd(int fd, unsigned int size, int direct_read)
{
	int ret;
	unsigned int rd_size = size;

	if (direct_read)
		rd_size = blocksize;

	ret = pread(fd, read_buf, rd_size, 0);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "read error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}
	if (ret != size) {
		fprintf(stderr, "Short read: wanted %d, got %d\n", size, ret);
		return -1;
	}

	return verify_pattern(size, read_buf);
}

int verify_pattern_fd(int fd, unsigned int size)
{
	return __verify_pattern_fd(fd, size, 0);
}

int verify_pattern_mmap(int fd, unsigned int size)
{
	int ret;
	unsigned int mmap_size = page_size;
	void *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_READ, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (read) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	ret = verify_pattern(size, region);

	munmap(region, mmap_size);

	return 0;
}

int uuid2dev(const char *uuid, char *dev)
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

int is_file_inlined(char *dirent_name, unsigned long *i_size,
		    unsigned int *id_count)
{
	int ret;
	uint64_t workplace_blk_no = 1;
	uint64_t testfile_blk_no = 1;
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
		goto bail;
	}

	/*lookup file inode,then read*/
	ret = ocfs2_lookup(fs, workplace_blk_no, dirent_name,
			   strlen(dirent_name), NULL, &testfile_blk_no);
	if (ret < 0) {

		fprintf(stderr, "failed to lookup file(%s/%s)'s"
			" inode blkno\n", work_place, dirent_name);
		goto bail;
	}

	ret = ocfs2_read_inode(fs, testfile_blk_no, buf);
	if (ret < 0) {
		fprintf(stderr, "failed to read file(%s/%s/%s)'s"
			" inode.\n", mount_point, WORK_PLACE, dirent_name);
		goto bail;
	}

	di = (struct ocfs2_dinode *)buf;
	*i_size = di->i_size;
	*id_count = ((di->id2).i_data).id_count;

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		ret = 1;
	else
		ret = 0;
bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

int should_inlined_or_not(int is_inlined, int should_inlined, int test_no)
{
	/* is_inlined represent if the ret is inlined or not
	   while should_inlined represnt if we expect it inlined or not
	*/
	if (should_inlined) {
		if (!is_inlined) {
			fprintf(stderr, "After Test #%d, file %s should be "
				"inlined here!\n", test_no, file_name);
			fprintf(stderr, "File(%s): i_size = %d,id_count = %d\n",
				file_name, i_size, id_count);
			return -1;
		}

	} else {
		if (is_inlined) {
			fprintf(stderr, "After Test #%d, file %s should be "
				"extented here!\n", test_no, file_name);
			fprintf(stderr, "File(%s): i_size = %d,id_count = %d\n",
				file_name, i_size, id_count);
			return -1;

		}
	}

	return 0;
}

