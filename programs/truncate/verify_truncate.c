/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * verify_truncate.c
 *
 * verification for truncating.
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

/*
 * This file is used to check whether both memory (page cache) and disk zero
 * the range between new i_size and the end of it's allocation(last cluster)
 *
 * The original file has been generated with a fixed number of clusters, after
 * truncating, the file content also will be verified from cluster to cluster.
 *
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ocfs2/ocfs2.h"

#define FILE_MODE		(S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)
#define FLAGS_RW		(O_CREAT|O_RDWR)

char *pattern;
char *last_pattern;
char *device;
char *filename;
ocfs2_filesys *fs;

unsigned long clustersize = 32768;
unsigned long clusters = 100;
long last_cluster;
unsigned long iter = 1;

int usage(void)
{
	fprintf(stdout, "verify_truncate <-f path> <-i iterations> "
		"<-c clusters> <-d device>\n");
	fprintf(stdout, "Example:\n"
		"	./verify_truncate /storage/testifle "
		"10 1000 /dev/sda8\n");

	return -1;
}

unsigned long long get_rand_ull(unsigned long long min,
				unsigned long long max)
{
	unsigned long long rand1, rand2, rand3, big_rand;

	if ((min == 0) && (max == 0))
		return 0;

	rand1 = (unsigned long long)rand();
	rand2 = (unsigned long long)rand();
	rand3 = (unsigned long long)rand();

	big_rand = (rand1 << 32) | (rand2 << 16) | (rand3);

	return min + (big_rand % (max - min + 1));
}

char rand_char(void)
{
	return 'A' + (char) get_rand_ull(0, 25);
}

int get_rand_buf(char *buf, unsigned long size)
{
	unsigned long i;

	for (i = 0; i < size; i++)
		buf[i] = rand_char();

	return 0;
}

int open_ocfs2_volume(const char *device)
{
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;
	int ret;
	uint64_t superblock = 0, block_size = 0;
	struct ocfs2_super_block *ocfs2_sb;

	ret = ocfs2_open(device, open_flags, superblock, block_size, &fs);
	if (ret < 0) {
		fprintf(stderr, "Failed to open ocfs2 fs on %s\n", device);
		return ret;
	}

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
	clustersize = 1 << ocfs2_sb->s_clustersize_bits;

	ocfs2_close(fs);
	return 0;
}

int prep_file_with_pattern(char *file_name, unsigned long long size,
			   unsigned long chunk_size, char *pattern_buf,
			   int once, int flags)
{
	int fd, fdt, ret, o_ret;
	unsigned long long offset = 0, write_size = 0;
	char tmp_path[PATH_MAX];

	fd = open64(file_name, flags, FILE_MODE);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	if (once) {

		while (offset < size) {
			if ((offset + chunk_size) > size)
				write_size = size - offset;
			else
				write_size = chunk_size;

			ret = pwrite(fd, pattern_buf, write_size, offset);
			if (ret < 0)
				return ret;

			offset += write_size;
		}
	} else {
		snprintf(tmp_path, PATH_MAX, "%s-tmp-file", file_name);
		fdt = open64(tmp_path, flags, FILE_MODE);

		unlink(tmp_path);

		while (offset < size) {

			if ((offset + chunk_size) > size)
				write_size = size - offset;
			else
				write_size = chunk_size;

			ret = pwrite(fd, pattern_buf, write_size, offset);
			if (ret < 0)
				return ret;

			ret = pwrite(fdt, pattern_buf, write_size, offset);
			if (ret < 0)
				return ret;

			offset += write_size;
		}

		close(fdt);

	}

	close(fd);
	return 0;
}

int file_truncate(char *file_name, unsigned long long new_i_size)
{
	int ret = 0;

	printf("Truncating %s to %lld\n", file_name, new_i_size);

	memcpy(last_pattern, pattern, clustersize);

	ret = truncate(file_name, new_i_size);
	if (ret) {
		ret = errno;
		fprintf(stderr, "Could not truncate %s to %llu bytes: %s\n",
			file_name, new_i_size, strerror(ret));
		return -1;
	}

	if ((new_i_size % clustersize) != 0)
		memset(last_pattern + new_i_size % clustersize, 0,
		       clustersize - new_i_size % clustersize);

	if ((new_i_size % clustersize) != 0)
		last_cluster = new_i_size / clustersize;
	else
		last_cluster = new_i_size / clustersize - 1;

	return ret;
}

int verify_truncate(char *file_name)
{

	int fd, ret = 0;
	int open_ro_flags = O_RDONLY;
	long i;

	char *buf = NULL;

	buf = malloc(clustersize);
	memset(buf, 0, clustersize);

	fd = open64(file_name, open_ro_flags, FILE_MODE);
	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		ret = -1;
		goto out;
	}

	printf("Verifying %s \n", file_name);

	/*
	 * Verify clusters from file start to 2nd last cluster.
	 */

	if (last_cluster < 0)
		goto out;

	for (i = 0; i < last_cluster - 1; i++) {

		ret = pread(fd, buf, clustersize, i * clustersize);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Pread on %s failed at %d: %s\n",
				file_name, ret, strerror(ret));
			ret = -1;
			goto out;
		}

		if (memcmp(buf, pattern, clustersize)) {
			fprintf(stderr, "#%ld cluster corrupted on %s.\n",
				i, file_name);
			ret = -1;
			goto out;
		}
	}

	/*
	 * Verify last cluster from page cache.
	 */
	ret = pread(fd, buf, clustersize, last_cluster * clustersize);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "Pread on %s failed at %d: %s\n", file_name,
			ret, strerror(ret));
		ret = -1;
		goto out;
	}

	if (memcmp(buf, last_pattern, clustersize)) {
		fprintf(stderr, "pagecache didn't get zero'd on %s\n",
			file_name);
		return -1;
	}

	fsync(fd);

	/*
	 * Verify last cluster from disk.
	 */
	ret = pread(fd, buf, clustersize, last_cluster * clustersize);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "Pread on %s failed at %d: %s\n",
			file_name, ret, strerror(ret));
		ret = -1;
		goto out;
	}

	if (memcmp(buf, last_pattern, clustersize)) {
		fprintf(stderr, "data on disk didn't get zero'd on %s\n",
			file_name);
		return -1;
	}

	ret = 0;

out:
	if (buf)
		free(buf);

	close(fd);

	return ret;
}

int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv, "i:f:c:d:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			iter = atol(optarg);
			break;
		case 'f':
			filename = optarg;
			break;
		case 'c':
			clusters = atol(optarg);
			break;
		case 'd':
			device = optarg;
			break;
		default:
			return -1;
		}
	}

	if (!device) {
		fprintf(stderr, "device is mandatory option\n");
		return -1;
	}

	if (!filename) {
		fprintf(stderr, "filename is mandatory option\n");
		return -1;
	}

	return 0;
}


int setup(int argc, char *argv[])
{

	int ret = 0;

	pattern = NULL;
	last_pattern = NULL;
	device = NULL;
	filename = NULL;
	fs = NULL;

	last_cluster = 0;

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return ret;
	}

	ret = open_ocfs2_volume(device);
	if (ret)
		return ret;

	srand(getpid());

	pattern = malloc(clustersize);
	last_pattern = malloc(clustersize);

	get_rand_buf(pattern, clustersize);
	memcpy(last_pattern, pattern, clustersize);

	return ret;
}

void teardown(void)
{
	if (pattern)
		free(pattern);

	if (last_pattern)
		free(last_pattern);
}

int run(void)
{
	int ret = 0;
	unsigned long long new_size = get_rand_ull(0, clusters * clustersize);

	ret = prep_file_with_pattern(filename, clusters * clustersize,
				     clustersize, pattern, 0, FLAGS_RW);
	if (ret)
		return ret;

	ret = file_truncate(filename, new_size);
	if (ret)
		return ret;

	if ((new_size % clustersize) != 0) {
		printf("Extend %s to %llu\n", filename,
		       (new_size / clustersize + 1) * clustersize);
		ret = truncate(filename,
			       (new_size / clustersize + 1) * clustersize);
		if (ret)
			return ret;
	}

	ret = verify_truncate(filename);
	if (ret)
		return ret;

	ret = unlink(filename);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	unsigned long i;

	ret = setup(argc, argv);
	if (ret)
		goto bail;

	for (i = 0; i < iter; i++) {
		ret = run();
		if (ret)
			goto bail;
	}

bail:
	teardown();
	return ret;
}
