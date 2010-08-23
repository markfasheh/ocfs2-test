/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * directio_test.c
 *
 * It's a generic tool to test O_DIRECT in following cases:
 *
 *   - In-place writes within i_size.
 *   - Apend writes outside i_size.
 *   - Writes within holes.
 *   - Destructive test.
 *
 * XXX: This could easily be turned into an mpi program.
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

#include "directio.h"

static void usage(void)
{
	printf("Usage: directio_test [-p concurrent_process] "
	       "[-l file_size] [-o logfile] <-w workfile>  -b -a -f "
	       "[-d <-A listener_addres> <-P listen_port>] -v -V\n"
	       "file_size should be multiples of 512 bytes\n"
	       "-v enable verbose mode."
	       "-b enable basic directio test within i_size.\n"
	       "-a enable append write test.\n"
	       "-f enable fill hole test.\n"
	       "-d enable destructive test, also need to specify the "
	       "listener address and port\n"
	       "-V enable verification test.\n\n");
	exit(1);
}

static int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv,
			   "h");
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			break;
		default:
			break;
		}
	}

	return 0;
}

static int setup(int argc, char *argv[])
{
	int ret = 0;

	if (parse_opts(argc, argv))
		usage();

	return ret;
}

static int teardown(void)
{
	int ret = 0;

	return ret;
}

static int run_test(void)
{
	int ret = 0;

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = setup(argc, argv);
	if (ret)
		return ret;

	ret = run_test();
	if (ret)
		return ret;

	ret = teardown();

	return ret;
}
