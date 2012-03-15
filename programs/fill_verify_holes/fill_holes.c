/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fill_holes.c
 *
 * Copyright (C) 2006, 2011 Oracle.  All rights reserved.
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
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <linux/types.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "fill_holes.h"
#include "reservations.h"
#include "aio.h"

static void usage(void)
{
	printf("fill_holes [-f] [-m] [-b] [-u] [-a] [-d] [-i ITER] [-o LOGFILE] [-r REPLAYLOG] FILE SIZE\n"
	       "FILE is a path to a file\n"
	       "SIZE is in bytes is required only for regular files, even with a REPLAYLOG\n"
	       "ITER defaults to 1000, unless REPLAYLOG is specified.\n"
	       "LOGFILE defaults to stdout\n"
	       "-f will result in logfile being flushed after every write\n"
	       "-m instructs the test to use mmap to write to FILE (size < 2G)\n"
	       "-b file is a block device\n"
	       "-u will create an unwritten region instead of ftruncate\n"
	       "-a will enable aio io mode\n"
	       "-d will enable direct io mode\n"
	       "REPLAYLOG is an optional file to generate values from\n\n"
	       "Regular files are truncated to zero and then truncated to SIZE.\n"
	       "FILE will be truncated to zero, then truncated out to SIZE\n"
	       "For each iteration, a character, offset and length will be\n"
	       "randomly generated or read from the optional REPLAYLOG and\n"
	       "written to FILE. The program ends after ITER iterations, or\n"
	       "until the end of the replay log, whichever comes first.\n"
	       "The exact patterns written will be logged such that\n"
	       "the log can be replayed by a verification program, or given\n"
	       "back to this software as a REPLAYLOG argument.\n"
	       "For files larger than 2G, the utility limits the randomness to 2G chunks.\n"
	       "It first writes randomly in the first 2G, then moves to the next 2G, etc.\n");
	exit(0);
}

#define ONE_MEGA_BYTE		(1024 * 1024)
#define TWO_GIGA_BYTE		2147483648
#define ALIGN(x, a)		(((x) + (a) - 1) & ~((a) - 1))
#define MAX_WRITE_SIZE		32768

static char *buf;

static uint32_t max_iter = 1000;
static uint64_t file_size;
static uint32_t max_iter_per_chunk = 0;
static uint32_t cur_iter_per_chunk = 0;
static uint64_t	cur_iter_offset = 0;

static uint8_t flush_output;
static uint8_t create_unwritten;
static uint8_t enable_aio;
static uint8_t use_mmap;
static uint8_t use_dio;
static uint8_t is_bdev;

static char *fname = NULL;
static char *logname = NULL;
static char *replaylogname = NULL;
static FILE *logfile = NULL;
static FILE *replaylogfile = NULL;
static void *mapped;

int fh_get_device_size(const char *device, uint64_t *size)
{
	int	fd;
	int	ret = -1;

	fd = open(device, O_RDONLY);
	if (fd > 0) {
		ret = ioctl(fd, BLKGETSIZE64, size);
		close(fd);
	}
	return ret;
}

static int fh_parse_opts(int argc, char **argv)
{
	int c, iter_specified = 0, ret, num_xtra_args;

	while (1) {
		c = getopt(argc, argv, "abdumfi:o:r:");
		if (c == -1)
			break;

		switch (c) {
		case 'u':
			create_unwritten = 1;
			break;
		case 'a':
			enable_aio = 1;
			break;
		case 'b':
			is_bdev = 1;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'd':
			use_dio = 1;
			break;
		case 'f':
			flush_output = 1;
			break;
		case 'i':
			max_iter = atoi(optarg);
			iter_specified = 1;
			break;
		case 'o':
			logname = optarg;
			break;
		case 'r':
			replaylogname = optarg;
			/*
			 * Trick the code into replaying until the log
			 * is empty.
			 */
			if (!iter_specified)
				max_iter = UINT_MAX;
			break;
		default:
			return EINVAL;
		}
	}
 
	num_xtra_args = 2;
	if (is_bdev)
		num_xtra_args = 1;

	if (argc - optind != num_xtra_args)
		return EINVAL;

	fname = argv[optind];
	if (!is_bdev) {
		file_size = strtoull(argv[optind + 1], NULL, 0);
		if (use_dio)
			file_size = ALIGN(file_size, ONE_MEGA_BYTE);
	} else {
		ret = fh_get_device_size(fname, &file_size);
		if (ret < 0) {
			fprintf(stderr, "Cannot determine device size\n");
			return EINVAL;
		}
	}

	if (use_mmap && (file_size > TWO_GIGA_BYTE || is_bdev))
		return EINVAL;

	return 0;
}

static int fh_resv_unwritten(int fd, uint64_t start, uint64_t len)
{
	int ret = 0;
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = 0;
	sr.l_start = start;
	sr.l_len = len;

	ret = ioctl(fd, OCFS2_IOC_RESVSP64, &sr);
	if (ret == -1) {
		fprintf(stderr, "ioctl error %d: \"%s\"\n",
			errno, strerror(errno));
		return -1;
	}

	return ret;
}

static int fh_prep_file(char *name, uint64_t size)
{
	int ret, fd;
	int flags = O_RDWR;
	mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;

	if (!is_bdev)
		flags |= O_CREAT|O_TRUNC;
	if (use_dio)
		flags |= O_DIRECT;

	fd = open(name, flags, mode);
	if (fd == -1) {
		fprintf(stderr, "open error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	if (create_unwritten) {
		ret = fh_resv_unwritten(fd, 0, size);
		if (ret)
			return ret;
	}

	if (!is_bdev) {
		ret = ftruncate(fd, size);
		if (ret == -1) {
			close(fd);
			fprintf(stderr, "ftruncate error %d: \"%s\"\n", errno,
				strerror(errno));
			return -1;
		}
	}

	if (use_mmap) {
		mapped = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (mapped == MAP_FAILED) {
			close(fd);
			fprintf(stderr, "mmap error %d: \"%s\"\n", errno,
				strerror(errno));
			return -1;
		}
	}

	return fd;
}

static int fh_open_logfile(void)
{
	if (!logname)
		logfile = stdout;
	else
		logfile = fopen(logname, "wa");
	if (!logfile) {
		fprintf(stderr, "Error %d creating logfile: %s\n", errno,
			strerror(errno));
		return EINVAL;
	}
	return 0;
}

static int fh_replay_eof(void)
{
	if (!replaylogfile)
		return 0;

	return feof(replaylogfile);
}

static int fh_open_replaylog(void)
{
	if (!replaylogname)
		return 0;

	replaylogfile = fopen(replaylogname, "r");
	if (!replaylogfile) {
		fprintf(stderr, "Error %d opening replay log: %s\n", errno,
			strerror(errno));
		return EINVAL;
	}
	return 0;
}

static void fh_log_write(struct fh_write_unit *wu)
{
	int fd;

	fprintf(logfile, "%c\t%"PRIu64"\t%u\n", wu->w_char, wu->w_offset,
		wu->w_len);

	if (flush_output) {
		fflush(logfile);
		fd = fileno(logfile);
		fsync(fd);
	}
}

static unsigned long fh_get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + ((rand() % max) - min);
}

static void fh_prep_rand_write_unit(struct fh_write_unit *wu)
{
	uint32_t max_range;

	if (max_iter_per_chunk) {
		max_range = TWO_GIGA_BYTE - 1;
		if ((cur_iter_per_chunk == max_iter_per_chunk)) {
			cur_iter_offset += TWO_GIGA_BYTE;
			cur_iter_per_chunk = 0;
		} else
			cur_iter_per_chunk++;
	} else {
		assert(file_size <= TWO_GIGA_BYTE);
		max_range = file_size - 1;
	}

	wu->w_char = RAND_CHAR_START + (char) fh_get_rand(0, 52);

again:
	wu->w_offset = fh_get_rand(0, max_range);
	if (use_dio)
		wu->w_offset = ALIGN(wu->w_offset, 512);
	wu->w_offset += cur_iter_offset;

	wu->w_len = (unsigned int) fh_get_rand(1, MAX_WRITE_SIZE);
	if (use_dio)
		wu->w_len = ALIGN(wu->w_len, 512);

	if (wu->w_offset + wu->w_len > file_size)
		wu->w_len = file_size - wu->w_offset;

	/* sometimes the random number might work out like this */
	if (wu->w_len == 0 || (use_dio && wu->w_len < 512))
		goto again;

	assert(wu->w_char >= RAND_CHAR_START && wu->w_char <= 'z');
	assert(wu->w_len <= MAX_WRITE_SIZE);
	assert(wu->w_len > 0);
}

static int fh_prep_write_unit(struct fh_write_unit *wu)
{
	int ret;

	if (!replaylogfile) {
		fh_prep_rand_write_unit(wu);
		return 0;
	}

	ret = fscanf(replaylogfile, "%c\t%"PRIu64"\t%u\n", &wu->w_char,
		     &wu->w_offset, &wu->w_len);
	if (ret != 3) {
		fprintf(stderr, "input failure from replay log, ret %d, %d %s\n",
			ret, errno, strerror(errno));
		return -EINVAL;
	}

	return 0;
}

int fh_do_write(int fd, struct fh_write_unit *wu)
{
	int ret, i;
	struct o2test_aio o2a;
	void *vbuf = NULL;
	char *buf_cmp;
	unsigned long long *ubuf = (unsigned long long *)buf;
	unsigned long long *ubuf_cmp = NULL;

	if (use_mmap) {
		memset(mapped + wu->w_offset, wu->w_char, wu->w_len);
		return 0;
	}

	memset(buf, wu->w_char, wu->w_len);

	if (enable_aio) {

		ret = posix_memalign((void **)&vbuf, 512, MAX_WRITE_SIZE);
		if (ret) {
			fprintf(stderr, "malloc error %d: \"%s\"\n", ret,
				strerror(ret));
			goto bail;
		}

		buf_cmp = vbuf; 

		ret = o2test_aio_setup(&o2a, 1);
		if (ret < 0)
			goto bail;

		ret = o2test_aio_pwrite(&o2a, fd, buf, wu->w_len, wu->w_offset);
		if (ret < 0)
			goto bail;

		ret = o2test_aio_query(&o2a, 1, 1);
		if(ret < 0)
			goto bail;

		ret = pread(fd, buf_cmp, wu->w_len, wu->w_offset);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "pread error %d: \"%s\"\n", ret,
				strerror(ret));
			ret = -1;
			goto bail;
		}

		if (memcmp(buf, buf_cmp, wu->w_len)) {
			ubuf_cmp = (unsigned long long *)buf_cmp;

			for (i = 0; i < wu->w_len / sizeof(unsigned long long); i++)
				printf("%d: 0x%llx[aio_write]  0x%llx[pread]\n",
				       i, ubuf[i], ubuf_cmp[i]);
		}

		ret = o2test_aio_destroy(&o2a);

		goto bail;
	}

	ret = pwrite(fd, buf, wu->w_len, wu->w_offset);
	if (ret == -1) {
		fprintf(stderr, "write error %d: \"%s\"\n", errno,
			strerror(errno));
		goto bail;
	}

	ret = 0;
bail:
	free(vbuf);

	return ret;
}

int main(int argc, char **argv)
{
	int ret, i, fd;
	struct fh_write_unit wu;
	void *vbuf = NULL;
	uint64_t num_chunks;

	if (argc < 3) {
		usage();
		return 1;
	}

	ret = fh_parse_opts(argc, argv);
	if (ret) {
		usage();
		return 1;
	}

	ret = posix_memalign((void **)&vbuf, 512, MAX_WRITE_SIZE);
	if (ret) {
		fprintf(stderr, "malloc error %d: \"%s\"\n", ret,
			strerror(ret));
		goto bail;
	}
	buf = vbuf;

	fd = fh_prep_file(fname, file_size);
	if (fd == -1)
		return 1;

	ret = fh_open_logfile();
	if (ret)
		return 1;

	ret = fh_open_replaylog();
	if (ret)
		return 1;

	srand(getpid());

	if (file_size > TWO_GIGA_BYTE) {
		num_chunks = file_size / TWO_GIGA_BYTE;
		file_size = num_chunks * TWO_GIGA_BYTE;
		max_iter_per_chunk = max_iter / num_chunks;
	}

	for(i = 0; (i < max_iter) && !fh_replay_eof(); i++) {
		ret = fh_prep_write_unit(&wu);
		if (ret)
			return 1;

		fh_log_write(&wu);

#if 0
		fprintf(stdout, "%6d. %c %"PRIu64", %"PRIu32"\n", i, wu.w_char,
			wu.w_offset, wu.w_len);
#endif
		ret = fh_do_write(fd, &wu);
		if (ret)
			return 1;
	}

bail:
	free(vbuf);

	return 0;
}
