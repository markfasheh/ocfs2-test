/*
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <linux/types.h>

#include "mpi.h"

#define DEFAULT_ITER     10

#define HOSTNAME_SIZE 50
static char hostname[HOSTNAME_SIZE];
static char *prog;
static char *path;
static int rank = -1, num_procs;
static unsigned long long max_iter = DEFAULT_ITER;

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void create_access(char *fname, int create)
{
	int ret, fd;

	if (create) {
		printf("%s: Create file \"%s\"\n", hostname, fname);

		fd = open(fname, O_CREAT|O_EXCL|O_WRONLY,
			  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if (fd == -1) {
			ret = errno;
			abort_printf("Error %d creating file \"%s\": %s\n",
				     ret, fname, strerror(ret));
		}

		close(fd);
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("First MPI_Barrier failed: %d\n", ret);

	ret = access(fname, R_OK);
	if (ret < 0) {
		ret = errno;
		abort_printf("Error %d accessing file \"%s\": %s\n",
			     ret, fname, strerror(ret));
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Second MPI_Barrier failed: %d\n", ret);
}

#define name_prefix "create_racer"

static void run_test(void)
{
	int i, len;
	char file[PATH_MAX];

	for(i = 0; i < max_iter; i++) {
		len = snprintf(file, PATH_MAX, "%s/%s:%04d",
			       path, name_prefix, i);
		if (len >= PATH_MAX)
			abort_printf("Path \"%s\" is too long\n", path);

		create_access(file, (i % num_procs) == rank);
	}
}

static void usage(void)
{
	printf("usage: %s [-i <iterations>] <path>\n", prog);
	printf("<iterations> defaults to %d\n", DEFAULT_ITER);
	printf("<path> is required.\n");
	printf("Will rotate through all processes, up to <iterations> times.\n");
	printf("In each pass, one node will create a file in the directory\n");
	printf("which <path> specifies, and the others will attempt to\n");
	printf("access the new file.\n");

	MPI_Finalize();
	exit(1);
}

int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "i:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			max_iter = atoll(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 1)
		return EINVAL;

	path = argv[optind];

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Init failed: %d\n", ret);
		exit(1);
	}

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	if (gethostname(hostname, HOSTNAME_SIZE) < 0) {
		fprintf(stderr, "gethostname failed: %s\n",
			strerror(errno));
		exit(1);
	}

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

        printf("%s: rank: %d, procs: %d, path \"%s\"\n",
	       hostname, rank, num_procs, path);

	run_test();

        MPI_Finalize();
        return 0;
}
