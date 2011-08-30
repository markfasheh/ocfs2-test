/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * defrager.c
 *
 * simple utility to defrag or move extents for a file.
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <ocfs2/bitops.h>
#include <libgen.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/image.h"
#include "ocfs2-kernel/ocfs2_ioctl.h"

char *program_name = NULL;
char *filename = NULL;

static void usage(void)
{
	fprintf(stderr, ("Usage: %s [-s start_offset] [-l length] "
		"[-t threshold] <file_path>\n"), program_name);
	exit(1);
}

int parse_opts(int argc, char **argv, struct ocfs2_move_extents *range)
{
	char c;

	program_name = argv[0];
	optind = 0;

	while((c = getopt(argc, argv, "s:l:ht:")) != EOF) {
		switch (c) {
		case 's':
			range->me_start = atol(optarg);
			break;
		case 'l':
			range->me_len = atol(optarg);
			break;
		case 't':
			range->me_threshold = atol(optarg);
			break;
		case 'h':
			usage();
			break;
		default:
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "No file specified\n");
		usage();
	}

	filename = argv[optind];

	return 0;
}

int main(int argc, char *argv[])
{
	
	int ret, fd;
	struct ocfs2_move_extents range;

	memset(&range, 0, sizeof(range));

	ret = parse_opts(argc, argv, &range);
	if (ret)
		return ret;

	range.me_flags |= OCFS2_MOVE_EXT_FL_AUTO_DEFRAG;

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "open file %s failed %d %s\n", filename,
			ret, strerror(ret));
		goto out;
	}

	ret = ioctl(fd, OCFS2_IOC_MOVE_EXT, &range);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "ioctl failed %d %s\n", ret, strerror(ret));
		goto out;
	}

	if (!(range.me_flags & OCFS2_MOVE_EXT_FL_COMPLETE))
		fprintf(stderr, "defrag didn't get finished completely.\n");
out:
	return ret;
}
