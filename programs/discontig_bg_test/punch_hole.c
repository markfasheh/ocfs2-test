/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * punch_hole.c
 *
 * A simple utility to punch a hole on a file.
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
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "resv.h"

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char *filename;
unsigned long long start;
unsigned long long len;

static int usage(void)
{
	fprintf(stdout, "punch_hole <-f path> <-s start> <-l length>\n");
	fprintf(stdout, "Example:\n"
			"       punch_hole -f /storage/testifle "
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

	return 0;
}

static int open_file(const char *filename)
{
	int fd = -1, ret = 0;
	int open_rw_flags = O_RDWR;

	fd = open64(filename, open_rw_flags, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", filename, ret,
			strerror(ret));
	}

	ret = fd;
	return ret;
}

static int punch_hole(int fd, uint64_t start, uint64_t len)
{
	int ret = 0;
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = 0;
	sr.l_start = start;
	sr.l_len = len;

	fprintf(stdout, "punching holes from %lu to %lu.\n",
		sr.l_start, sr.l_start + sr.l_len);

	ret = ioctl(fd, OCFS2_IOC_UNRESVSP64, &sr);
	if (ret == -1) {
		fprintf(stderr, "ioctl error %d: \"%s\"\n",
			errno, strerror(errno));
		return -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int fd, ret = 0;

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return -1;
	}

	fd = open_file(filename);
	if (fd < 0) {
		fprintf(stderr, "Open file %s failed.\n", filename);
		ret = -1;
	}

	ret = punch_hole(fd, start, len);
	if (ret < 0)
		fprintf(stderr, "Punch hole failed.\n");

	if (fd > 0)
		close(fd);

	return ret;
}
