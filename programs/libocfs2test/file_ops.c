/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file_ops.c
 *
 * Provide generic utility fuctions on file operations for ocfs2-tests
 *
 * Written by tristan.ye@oracle.com
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

#include "file_ops.h"

unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

char get_rand_char(void)
{
	return 'A' + (char) get_rand(0, 25);
}

unsigned int get_rand_nam(char *str, unsigned int least, unsigned int most)
{
	unsigned nam_len = get_rand(least, most);
	unsigned i = 0;

	memset(str, 0, OCFS2_MAX_FILENAME_LEN);

	while (i < nam_len) {
		str[i] = get_rand_char();
		i++;
	}

	str[nam_len] = '\0';

	return nam_len;
}

int read_at(int fd, void *buf, size_t count, off_t offset)
{
	int ret;
	size_t bytes_read;

	ret = pread(fd, buf, count, offset);

	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "read error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	bytes_read = ret;
	while (bytes_read < count) {

		ret = pread(fd, buf + bytes_read, count - bytes_read, offset +
			    bytes_read);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "read error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}

		bytes_read += ret;
	}

	return 0;
}

int read_at_file(char *pathname, void *buf, size_t count, off_t offset)
{
	int fd, ret;

	fd  = open64(pathname, FILE_RO_FLAGS);
	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		return fd;
	}

	ret = read_at(fd, buf, count, offset);

	if (ret < 0)
		return ret;

	close(fd);

	return 0;
}

int mmap_read_at(int fd, char *buf, size_t count, off_t offset,
			size_t page_size)
{
	int ret;
	unsigned long mmap_size = page_size;
	unsigned long size = offset + count;
	char *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_READ, MAP_SHARED, fd, 0);

	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (read) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	memcpy(buf, region + offset, count);

	munmap(region, mmap_size);

	return 0;
}

int mmap_read_at_file(char *pathname, void *buf, size_t count,
			     off_t offset, size_t page_size)
{
	int fd, ret;

	fd  = open64(pathname, FILE_RO_FLAGS);

	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		return fd;
	}

	ret = mmap_read_at(fd, buf, count, offset, page_size);

	if (ret < 0)
		return ret;

	close(fd);

	return ret;
}

int write_at(int fd, const void *buf, size_t count, off_t offset)
{
	int ret;

	size_t bytes_write;

	ret = pwrite(fd, buf, count, offset);

	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "write error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	bytes_write = ret;
	while (bytes_write < count) {

		ret = pwrite(fd, buf + bytes_write, count - bytes_write,
			     offset + bytes_write);

		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "write error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}

		bytes_write += ret;
	}

	return 0;
}

int write_at_file(char *pathname, const void *buf, size_t count,
		  off_t offset)
{
	int fd, ret;

	fd  = open64(pathname, FILE_RW_FLAGS);

	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		return fd;
	}

	ret = write_at(fd, buf, count, offset);

	if (ret < 0)
		return ret;

	close(fd);

	return 0;
}

int mmap_write_at(int fd, const char *buf, size_t count, off_t offset,
			 size_t page_size)
{
	int ret;
	unsigned long mmap_size = page_size;
	unsigned long size = offset + count;
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

	memcpy(region + offset, buf, count);

	munmap(region, mmap_size);

	return 0;
}

int mmap_write_at_file(char *pathname, const void *buf, size_t count,
			      off_t offset, size_t page_size)
{
	int fd, ret;

	fd  = open64(pathname, FILE_RW_FLAGS);

	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		return fd;
	}

	ret = mmap_write_at(fd, buf, count, offset, page_size);

	if (ret < 0)
		return ret;

	close(fd);

	return 0;
}

int reflink(const char *oldpath, const char *newpath)
{
	int fd, ret;
	struct reflink_arguments args;

	args.old_path = (__u64)oldpath;
	args.new_path = (__u64)newpath;

	fd = open64(oldpath, FILE_RO_FLAGS);

	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", oldpath, fd,
			strerror(fd));
		return fd;
	}

	ret = ioctl(fd, OCFS2_IOC_REFLINK, &args);

	if (ret) {
		ret = errno;
		fprintf(stderr, "ioctl failed:%d:%s\n", ret, strerror(ret));
		return ret;
	}

	close(fd);

	return 0;
}

int do_write(int fd, struct write_unit *wu, int write_method, size_t page_size)
{
	int ret;
	char buf[MAX_WRITE_SIZE];

	memset(buf, wu->w_char, wu->w_len);

	if (write_method == 1)
		ret = mmap_write_at(fd, buf, wu->w_len, wu->w_offset,
				    page_size);
	else
		ret = write_at(fd, buf, wu->w_len, wu->w_offset);

	return ret;
}

int do_write_file(char *fname, struct write_unit *wu, int write_method,
			 size_t page_size)
{
	int fd, ret;

	fd  = open64(fname, FILE_RW_FLAGS);
	if (fd < 0) {

		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", fname, fd,
			strerror(fd));
		return fd;
	}

	ret = do_write(fd, wu, write_method, page_size);

	close(fd);

	return ret;
}

int get_bs_cs(char *device_name, unsigned int *bs, unsigned long *cs,
	      unsigned long *max_inline_sz)
{
	int ret;
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;

	ocfs2_filesys *fs;
	struct ocfs2_super_block *ocfs2_sb;

	ret = ocfs2_open(device_name, open_flags, 0, 0, &fs);

	if (ret < 0) {
		com_err("CurrentTest", ret,
			"while opening file system.");
		return ret;
	}

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);

	*bs = fs->fs_blocksize;
	*cs = fs->fs_clustersize;
	*max_inline_sz = ocfs2_max_inline_data(*bs);

	ocfs2_close(fs);

	return 0;
}

int punch_hole(int fd, struct write_unit *wu)
{
	int ret;
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = 0;
	sr.l_start = wu->w_offset;
	sr.l_len = wu->w_len;

	ret = ioctl(fd, OCFS2_IOC_UNRESVSP64, &sr);
	if (ret == -1) {
		fprintf(stderr, "ioctl error %d: \"%s\"\n",
			errno, strerror(errno));
		return -1;
	}

	return 0;
}
