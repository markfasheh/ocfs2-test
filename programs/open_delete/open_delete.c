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
 *
 * XXX: Future improvements:
 *      Allow one reader, multiple writers
 *      All writers.
 *      Fill/Verify patterns in other interesting orders:
 *        -backwards,
 *        -start at block X
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "mpi.h"

#define HOSTNAME_SIZE 50
static char hostname[HOSTNAME_SIZE];
static int rank = -1, num_procs;

/* Variables influenced by runtime options */
static char * filename;
static unsigned int max_passes = 1;

static int this_pass;

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void usage(void)
{
	printf("open_delete_test [-i <iter>] filename\n\n"
	"Requires at least two processes. The rank zero process preps\n"
	"a file by O_CREAT|O_TRUNC and keep opening it.\n"
	"All nodes then delete this file.\n\n"
	"OPTIONS:\n"
	"-i <iter>\tNumber of times to pass through the file. Default is 1\n");
	MPI_Finalize();
	exit(1);
}

static int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "i:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			max_passes = atoi(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 1)
		return EINVAL;

	filename = argv[optind];

	return 0;
}

static int open_delete_test(void)
{
	int ret, fd;

	if (rank)
		goto open_after;

	fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd == -1) {
		ret = errno;
		abort_printf("Error %d opening file \"%s\": %s\n",
			     ret, filename, strerror(ret));
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	goto delete_after;

open_after:
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	ret = unlink(filename);
	if ( ret ) {
		ret = errno;
		printf("%s (rank %d): ", hostname, rank);
		printf("Error %d deleteing file \"%s\": %s\n",
			     ret, filename, strerror(ret));
	} else{
		printf("%s (rank %d): ", hostname, rank);
		printf("Successfully deleted file \"%s\" (%d)\n",
			     filename, ret);
	}
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	return ret;

delete_after:
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	ret = close(fd);

	fd = open(filename, O_RDONLY);
	if ( fd != -1 ) {
		abort_printf("Unexpected success when open deleted file: %s \n", filename);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int ret ;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Init failed: %d\n", ret);
		exit(1);
	}

	if (parse_opts(argc, argv))
		usage();

	if (gethostname(hostname, HOSTNAME_SIZE) < 0) {
		fprintf(stderr, "gethostname failed: %s\n",
			strerror(errno));
		exit(1);
	}

	strcat(filename, "_");
	strcat(filename, "open_and_delete-test-file");

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

	if (num_procs > 60 || num_procs < 2)
		abort_printf("Process count should be between 2 and 60\n");

        printf("%s: rank: %d, procs: %d, filename \"%s\"\n",
	       hostname, rank, num_procs, filename);

	for (this_pass = 0; this_pass < max_passes; this_pass++) {
		open_delete_test();
	}

        MPI_Finalize();

	return 0;
}
