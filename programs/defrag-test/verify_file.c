/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * verify_file.c
 *
 * A simple utility to verify file's data in chunks.
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "file_verify.h"

char *filename = NULL, *logname = NULL;
unsigned long filesize = 0;
unsigned long chunksize = 0;
union log_handler r_log;
int verbose = 0;

static int usage(void)
{
	fprintf(stdout, "frager <-f file> <-o log> <-l filesize> "
		"<-k chunksize> <-v>\n");
	fprintf(stdout, "Example:\n"
			"       ./verify_file -f /storage/testfile -o "
		"logs/logfile -l 104857600 -k 32768\n");
	exit(1);
}

int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv, "f:o:l:hvk:");
		if (c == -1)
			break;

		switch (c) {
		case 'l':
			filesize = atol(optarg);
			break;
		case 'k':
			chunksize = atol(optarg);
			break;
		case 'f':
			filename = optarg;
		case 'o':
			logname = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage();
			break;
		default:
			return -1;
		}
	}

	return 0;
}

int setup(int argc, char *argv[])
{
	int ret = 0;
	FILE *logfile = NULL;

	if (parse_opts(argc, argv)) {
		printf("parse_opts failed\n");
		usage();
	}
	
	if ((!filename) || (!logname)) {
		fprintf(stderr, "filename and logname is a mandatory"
			" option.\n");
		usage();
	}

	if ((!filesize) || (!chunksize)) {
		fprintf(stderr, "filesize and chunksize is a mandatory "
			"option.\n");
		usage();
	}

	ret = open_logfile(&logfile, logname, 1);
	r_log.stream_log = logfile;
	
	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	ret = setup(argc, argv);
	if (ret)
		return ret;

	ret = verify_file(0, r_log.stream_log, NULL, filename, filesize,
			  chunksize, verbose);

	return ret;
}
