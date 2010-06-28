/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * gen_extents.c
 *
 * A simple utility to generate file with extents(one cluster for each).
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
#define _LARGEFILE64_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <inttypes.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char filename[PATH_MAX];
unsigned long long filesize;
unsigned long long chunksize = 32768;
int keep = 1;
int enable_multi_nodes;

static int usage(void)
{
	fprintf(stdout, "gen_extents <-f path> <-l filesize> <-c chunksize> "
		"[-k keep] [-m]\n");
	fprintf(stdout, "Example:\n"
		"       gen_extents -f /storage/testifle -l 1073741824 -c 4096"
		"-k 1\n");

	exit(1);
}

int parse_opts(int argc, char **argv)
{
	char c;
	char hostname[256];

	filesize = 0;
	enable_multi_nodes = 0;

	while (1) {
		c = getopt(argc, argv, "f:l:c:k:mh:");
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			strncpy(filename, optarg, PATH_MAX);
			break;
		case 'l':
			filesize = atoll(optarg);
			break;
		case 'c':
			chunksize = atoll(optarg);
			break;
		case 'k':
			keep = atoi(optarg);
			break;
		case 'm':
			enable_multi_nodes = 1;
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

	if (!filesize) {
		fprintf(stderr, "filesize is zero? NOOP!\n");
		exit(0);
	}

	if (!chunksize) {
		fprintf(stderr, "chunksize is a mandartory option\n");
		usage();
	}

	if (enable_multi_nodes) {
		if (gethostname(hostname, 256) < 0) {
			fprintf(stderr, "gethostname failed.\n");
			exit(1);
		}
		strcat(filename, hostname);
	}

	return 0;
}

/*
 * How to construct a file with a plenty of extents:
 *
 * Write 2 files by turn, one is our target file, another is
 * a temporary one, each write is in chunk size, as a result.
 * the desired file finially will be made up of a plenty of
 * extents in a expected number.
 */
int prep_file(char *file_name, uint64_t size, uint64_t chunksize, int keep)
{
	int fd = -1, fdt = -1, ret = 0;
	unsigned long long offset = 0, write_size = 0;
	char tmp_path[PATH_MAX];

	char *pattern = NULL;
	int open_rw_flags = O_CREAT|O_RDWR|O_APPEND|O_TRUNC;

	pattern = (char *)malloc(chunksize);

	fd = open64(file_name, open_rw_flags, FILE_MODE);
	if (fd < 0) {
		ret = fd;
		fd = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		goto bail;
	}

	memset(pattern, 'a', chunksize);

	snprintf(tmp_path, PATH_MAX, "%s-tmp-file", file_name);

	fdt = open64(tmp_path, open_rw_flags, FILE_MODE);
	if (fdt < 0) {
		ret = fdt;
		fdt = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n",
			tmp_path, fdt, strerror(fdt));
		goto bail;
	}

	if (!keep)
		unlink(tmp_path);

	while (offset < size) {

		if ((offset + chunksize) > size)
			write_size = size - offset;
		else
			write_size = chunksize;

		ret = pwrite(fd, pattern, write_size, offset);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "pwrite on %s failed at "
				"%llu:%d:%s\n", file_name, offset,
				ret, strerror(ret));
			goto bail;
		}

		ret = pwrite(fdt, pattern, write_size, offset);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "pwrite on %s failed at "
				"%llu:%d:%s\n", tmp_path, offset,
				ret, strerror(ret));
			goto bail;
		}

		offset += write_size;
	}

bail:
	if (pattern)
		free(pattern);

	if (fdt > 0)
		close(fdt);

	if (fd > 0)
		close(fd);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return ret;
	}

	ret = prep_file(filename, filesize, chunksize, keep);

	return ret;
}
