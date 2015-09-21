/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * check_file_size_limits.c
 *
 * Tests file I/O at the maximum offsets that Ocfs2 supports
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static unsigned long page_cache_size;
static unsigned int config_lbd = 1;
static unsigned int bits_per_long = 8 * (sizeof(long));

static void usage(void)
{
	printf("usage: "
	       "check_file_size_limits [-B bits_per_long] [-P page_cache_size] "
	       "[-L CONFIG_LBD] -b <blocksize bits> -c <clustersize bits> "
	       "filename\n\n"
	       "Tests file I/O at the maximum offsets that Ocfs2 supports.\n"
	       "Right now, this involves two tests.\n\n"
	       "The first test makes sure that a truncate that's too large "
	       "properly returns an error.\n\n"
	       "The second test will attempt a small write which ends at the "
	       "largest possible offset. The data is then read back in to "
	       "ensure that it is coherent.\n\n"
	       "blocksize bits and clustersize bits are required.\n"
	       "This software will make a reasonable attempt at guessing page "
	       "cache size, bits per long and whether large block device "
	       "support is enabled. page_cache_size is the only one that can "
	       "be completely trusted though.\n\n"
	       "On 64 bit platforms with 32 bit userspace (ppc has this) "
	       "bits_per_long should be forced to 64.\n"
	       "CONFIG_LBD is just assumed to be enabled.\n");
}

/*
 * Taken from fs/ocfs2/super.c
 *
 * Keep this in sync with the kernel.
 */
static unsigned long long ocfs2_max_file_offset(unsigned int bbits,
						unsigned int cbits)
{
	unsigned int bytes = 1 << cbits;
	unsigned int trim = bytes;
	unsigned int bitshift = 32;

	/*
	 * i_size and all block offsets in ocfs2 are always 64 bits
	 * wide. i_clusters is 32 bits, in cluster-sized units. So on
	 * 64 bit platforms, cluster size will be the limiting factor.
	 */

	if (bits_per_long == 32) {
		if (config_lbd) {
			/*
			 * We might be limited by page cache size.
			 */
			if (bytes > page_cache_size) {
				bytes = page_cache_size;
				trim = 1;
				/*
				 * Shift by 31 here so that we don't
				 * get larger than MAX_LFS_FILESIZE
				 */
				bitshift = 31;
			}
		} else {
			/*
			 * We are limited by the size of sector_t. Use
			 * block size, as that's what we expose to the
			 * VFS.
			 */
			bytes = 1 << bbits;
			trim = 1;
			bitshift = 31;
		}
	}

	/*
	 * Trim by a whole cluster when we can actually approach the
	 * on-disk limits. Otherwise we can overflow i_clusters when
	 * an extent start is at the max offset.
	 */
	return (((unsigned long long)bytes) << bitshift) - trim;
}

static int parse_opts(int argc, char **argv, unsigned int *bbits,
		      unsigned int *cbits, char **fname)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "B:P:L:b:c:");

		if (c == -1)
			break;

		switch (c) {
		case 'L':
			config_lbd = atoi(optarg);
			break;
		case 'P':
			page_cache_size = atol(optarg);
			break;
		case 'B':
			bits_per_long = atoi(optarg);
			break;
		case 'b':
			*bbits = atoi(optarg);
			break;
		case 'c':
			*cbits = atoi(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 1)
		return EINVAL;

	*fname = argv[optind];

	return 0;
}

static void init_consts(void)
{
	page_cache_size = sysconf(_SC_PAGESIZE);
}

static int test_for_efbig(int fd, unsigned long long max_off)
{
	int ret;

	max_off++;
	ret = ftruncate64(fd, max_off);
	if (ret == 0 || (ret == -1 && errno != EFBIG)) {
		fprintf(stderr, "Test to ftruncate past max_off failed\n");
		return EINVAL;
	}

	return 0;
}

#define TEST_STR "Hello World"
#define TEST_STR_LEN (sizeof(TEST_STR) - 1)
static int write_at_largest_off(int fd, unsigned long long max_off)
{
	int ret;
	loff_t lret;
	unsigned long long offset = max_off - TEST_STR_LEN;
	char buf[TEST_STR_LEN + 1]; 	/* add 1 so we can pretty print it */

	memset(buf, 0, TEST_STR_LEN + 1);

	lret = lseek64(fd, offset, SEEK_SET);
	if (lret == -1) {
		fprintf(stderr, "Seek error %d: \"%s\"\n", errno,
			strerror(errno));
		return EINVAL;
	}
	
	ret = write(fd, TEST_STR, TEST_STR_LEN);
	if (ret == -1) {
		fprintf(stderr, "Write error %d: \"%s\"\n", errno,
			strerror(errno));
		return EINVAL;
	}

	lret = lseek64(fd, offset, SEEK_SET);
	if (lret == -1) {
		fprintf(stderr, "Seek error %d: \"%s\"\n", errno,
			strerror(errno));
		return EINVAL;
	}

	ret = read(fd, buf, TEST_STR_LEN);
	if (ret == -1) {
		fprintf(stderr, "Read error %d: \"%s\"\n", errno,
			strerror(errno));
		return EINVAL;
	}

	if (strncmp(TEST_STR, buf, TEST_STR_LEN) != 0) {
		fprintf(stderr, "Buffer read back does not match\n");
		fprintf(stderr, "Wrote: %s\n", TEST_STR);
		fprintf(stderr, "Read : %s\n", buf);
		return EINVAL;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int fd, ret;
	int bbits = 0, cbits = 0;
	char *fname = NULL;
	unsigned long long max_off;

	init_consts();

	signal(SIGXFSZ, SIG_IGN);

	if (parse_opts(argc, argv, &bbits, &cbits, &fname)) {
		usage();
		return 1;
	}

	if (bbits == 0 || cbits == 0) {
		usage();
		return 1;
	}

	max_off = ocfs2_max_file_offset(bbits, cbits);
	printf("CONFIG: bbits: %d, cbits: %d, page size: %lu, config_lbd: %u, "
	       "bits_per_long: %u, max_off: %llu\n",
	       bbits, cbits, page_cache_size, config_lbd, bits_per_long,
	       max_off);

	fd = open(fname, O_TRUNC|O_CREAT|O_RDWR|O_LARGEFILE,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		fprintf(stderr, "open error %d: \"%s\"\n", errno,
			strerror(errno));
		ret = errno;
		return ret;
	}

	ret = test_for_efbig(fd, max_off);
	if (ret)
		goto out_close;

	ret = write_at_largest_off(fd, max_off);
	if (ret)
		goto out_close;

	printf("All tests passed.\n");

out_close:
	close(fd);
	return ret;
}
