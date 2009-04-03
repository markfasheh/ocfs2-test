/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mpi_ops.c
 *
 * Provide generic utility fuctions on mpi operations for ocfs2-tests
 *
 * Written by tristan.ye@oracle.com
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include "mpi_ops.h"

extern int rank, size;
extern char *hostname;

void abort_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

void root_printf(const char *fmt, ...)
{
	va_list ap;

	if (rank == 0) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
	}
}

void MPI_Barrier_Sync(void)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

int MPI_Setup(int argc, char *argv[])
{
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Init failed!\n");

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0)
		abort_printf("Get hostname failed!\n");

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

	return 0;
}
