/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi_directio_test.c
 *
 * A mpi compatible program for directio multiple-nodes testing,
 * it was based on testcases designed for directio_test.c,which
 * will be executed concurrently among multiple nodes.
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
#include <mpi.h>

#define MPI_RET_SUCCESS		0
#define MPI_RET_FAILED		1

static char *prog;
static char workfile[PATH_MAX];
static char hostname[PATH_MAX];

static int rank = -1, size = 0;

int verbose = 0;
int test_flags = 0x00000000;
int open_rw_flags = FILE_RW_FLAGS;
int open_ro_flags = FILE_RO_FLAGS;

unsigned long file_size = 1024 * 1024;
unsigned long num_chunks;
unsigned long num_iterations = 1;

struct write_unit *remote_wus = NULL;

static void usage(void)
{
	printf("usage: %s [-i <iters>] [-l <file_size>] [-w <workfile>] "
	       "[-v]\n", prog);

	MPI_Finalize();

	exit(1);
}

static void rank_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);
}

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void MPI_Barrier_Sync(void)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

static int parse_opts(int argc, char **argv);

static void setup(int argc, char *argv[])
{
	unsigned long i;
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

	num_chunks = file_size / CHUNK_SIZE;

	fflush(stderr);
	fflush(stdout);

	remote_wus = (struct write_unit *)malloc(sizeof(struct write_unit) *
						 num_chunks);
	memset(remote_wus, 0, sizeof(struct write_unit) * num_chunks);

	for (i = 0; i < num_chunks; i++)
		remote_wus[i].wu_chunk_no = i;

	if (gethostname(hostname, PATH_MAX) < 0) {
		perror("gethostname:");
		exit(1);
	}

	srand(getpid());

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);
	ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

	return;
}

static void teardown(int ret_type)
{
	if (remote_wus)
		free(remote_wus);

	if (ret_type == MPI_RET_SUCCESS) {
		MPI_Finalize();
		exit(0);
	} else {
		fprintf(stderr, "Rank:%d on Host(%s) abort!\n",
			rank, hostname);
		MPI_Abort(MPI_COMM_WORLD, 1);
		exit(1);
	}
}

static void should_exit(int ret)
{
	if (ret < 0) {
		fprintf(stderr, "Rank:%d on Host(%s) abort!\n", rank,
			hostname);
		teardown(MPI_RET_FAILED);
	}
}

static int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "i:l:vw:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			num_iterations = atol(optarg);
			break;
		case 'l':
			file_size = atol(optarg);
			break;
		case 'w':
			strcpy(workfile, optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			return EINVAL;
		}
	}

	if (strcmp(workfile, "") == 0)
		return -EINVAL;

	if ((file_size % DIRECTIO_SLICE) != 0) {
		fprintf(stderr, "file size is expected to be %d aligned.\n",
			CHUNK_SIZE);
		return -EINVAL;
	}

	return 0;
}

static int one_round_run(int round_no)
{
	int ret = 0, fd = -1, j;
	unsigned long i, chunk_no = 0;
	struct write_unit wu;

	MPI_Request request;
	MPI_Status  status;

	/*
	 * Root rank creates working file in chunks.
	 */
	if (!rank) {
		rank_printf("Prepare file of %lu bytes\n", file_size);

		open_rw_flags |= O_DIRECT;
		open_ro_flags |= O_DIRECT;

		ret = prep_orig_file_in_chunks(workfile, file_size);
		should_exit(ret);
	}

	MPI_Barrier_Sync();

	if (!rank) {
		fd = open_file(workfile, open_rw_flags);
		should_exit(fd);
	} else {

		/*
		 * Verification at the very beginning doesn't do anything more
		 * than reading the file into pagecache on none-root nodes.
		 */
		open_rw_flags &= ~O_DIRECT;
		open_ro_flags &= ~O_DIRECT;

		ret = verify_file(1, NULL, remote_wus, workfile, file_size);
		should_exit(fd);
	}

	MPI_Barrier_Sync();

	/*
	 * Root ranks write chunks at random serially.
	 */
	for (i = 0; i < num_chunks; i++) {
		
		MPI_Barrier_Sync();
		/*
		 * Root rank generates random write unit, then sends it to
		 * rest of ranks in-memoery after O_DIRECT write into file.
		 */
		if (!rank) {

			chunk_no = get_rand_ul(0, num_chunks - 1);
			prep_rand_dest_write_unit(&wu, chunk_no);
			rank_printf("Write #%lu chunk with char(%c)\n",
				    chunk_no, wu.wu_char);
			ret = do_write_chunk(fd, wu);
			should_exit(ret);
			
			memcpy(&remote_wus[wu.wu_chunk_no], &wu, sizeof(wu));

			for (j = 1; j < size; j++) {
				if (verbose)
					rank_printf("Send write unit #%lu chunk "
						    "char(%c) to rank %d\n",
						     wu.wu_chunk_no,
						     wu.wu_char, j);
				ret = MPI_Isend(&wu, sizeof(wu), MPI_BYTE, j,
						1, MPI_COMM_WORLD, &request);
				if (ret != MPI_SUCCESS)
					abort_printf("MPI_Isend failed: %d\n",
						     ret);
				MPI_Wait(&request, &status);

                        }
		} else {

			MPI_Irecv(&wu, sizeof(wu), MPI_BYTE, 0, 1,
				  MPI_COMM_WORLD, &request);
			MPI_Wait(&request, &status);

			if (verbose)
				rank_printf("Receive write unit #%lu chunk "
					    "char(%c)\n", wu.wu_chunk_no, wu.wu_char);

			if (wu.wu_timestamp >=
				remote_wus[wu.wu_chunk_no].wu_timestamp)
				memcpy(&remote_wus[wu.wu_chunk_no],
				       &wu, sizeof(wu));
		}

		MPI_Barrier_Sync();

		if (rank) {

			/*
			 * All none-root ranks need to verify if O_DIRECT writes
			 * from remote root node can be seen locally.
			 */
			rank_printf("Try to verify whole file in chunks.\n");

			ret = verify_file(1, NULL, remote_wus, workfile, file_size);
			should_exit(ret);
		}
	}

	MPI_Barrier_Sync();

	if (!rank)
		if (fd > 0)
			close(fd);

	return ret;
}
static int test_runner(void)
{
	int i;
	int ret = 0;

	for (i = 0; i < num_iterations; i++) {
		if (rank == 0) {
			printf("**************************************"
			       "****************\n");
			printf("**************Round %d test running..."
			       "*****************\n", i);
			printf("**************************************"
			       "****************\n");
			fflush(stdout);
		}

		ret = one_round_run(i);
		if (ret)
			return ret;

		MPI_Barrier_Sync();
	}

	return ret;
}
int main(int argc, char *argv[])
{
	int ret = 0;

	setup(argc, argv);
	ret = test_runner();
	teardown(MPI_RET_SUCCESS);

	return ret;
}
