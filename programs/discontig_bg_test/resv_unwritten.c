/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * resv_unwritten.c
 *
 * A simple utility to alloc a region of unwritten space.
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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resv.h"

#define FILE_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
			 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char *filename;
unsigned long long start;
unsigned long long len;

static int usage(void)
{
	fprintf(stdout, "resv_unwritten <-f path> <-s start> <-l length>\n");
	fprintf(stdout, "Example:\n"
			"       resv_unwritten -f /storage/testifle "
		"-s 0 -l 1073741824\n");

	exit(1);
}

int parse_opts(int argc, char **argv)
{
	char c;

	filename = NULL;
	start = 0;
	len = 0;

	while (1) {
		c = getopt(argc, argv, "f:s:l:h:");
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 's':
			start = atoll(optarg);
			break;
		case 'l':
			len = atoll(optarg);
			break;
		case 'h':
			usage();
			break;
		default:
			return -1;
		}
	}

	if (!filename) {
		fprintf(stderr, "filename is a mandatory option\n");
		usage();
	}

	if (!len) {
		fprintf(stderr, "length is a mandartory option\n");
		usage();
	}

	return 0;
}

static int open_file(const char *filename)
{
	int fd = -1, ret = 0;
	int open_rw_flags = O_RDWR|O_CREAT;

	fd = open64(filename, open_rw_flags, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", filename, ret,
			strerror(ret));
	}

	ret = fd;

	return ret;
}

static int resv_unwritten(int fd, uint64_t start, uint64_t len)
{
	int ret;
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = 0;
	sr.l_start = start;
	sr.l_len = len;

	fprintf(stdout, "reserve unwritten region from %lu to %lu.\n",
		sr.l_start, sr.l_start + sr.l_len);
	ret = ioctl(fd, OCFS2_IOC_RESVSP64, &sr);
	if (ret < 0) {
		fprintf(stderr, "ioctl error %d: \"%s\"\n",
			errno, strerror(errno));
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0, fd;

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return -1;
	}

	fd = open_file(filename);
	if (fd < 0)
		return fd;

	ret = resv_unwritten(fd, start, len);
	if (ret < 0)
		fprintf(stderr, "resv allocation failed %s\n", strerror(ret));

	return ret;
}
