/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi_defrager.c
 *
 * A mpi compatible program to test defragmentation on ocfs2
 * concurently among multiple nodes.
 *
 * Written by tristan.ye@oracle.com
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <mpi.h>

#include <ocfs2/ocfs2.h>

#define HOSTNAME_MAX_SZ		100
#define MPI_RET_SUCCESS		0
#define MPI_RET_FAILED		1

#define FILE_MODE		(S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char *prog;

static char workplace[PATH_MAX];
static char hostname[HOSTNAME_MAX_SZ];

static int rank = -1, size;

static int do_random = 0;
static int verbose = 0;

static void rank_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);
}

static void abort_printf(const char *fmt, ...)
{
	va_list ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void root_printf(const char *fmt, ...)
{
	va_list ap;

	if (rank == 0) {
		va_start(ap, fmt);
		vprintf(fmt, ap);
	}

}

static void usage(void)
{
       root_printf("Usage: multi_defrager [-s start] [-l len] [-t threshold] "
		   "[-w working directory] [-r] [-v]\n");
	MPI_Finalize();
	exit(1);

}

int parse_opts(int argc, char **argv, struct ocfs2_move_extents *range)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "s:l:vhrt:w:");
		if (c == -1)
			break;

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
		case 'w':
			strcpy(workplace, optarg);
			break;
		case 'r':
			do_random = 1;
			break;
		case 'v':
			verbose = 1;
			break;	
		case 'h':
			usage();
			break;
		default:
			break;
		}
	}

	if (strcmp(workplace, "") == 0)
		return EINVAL;

	return 0;
}

static void MPI_Barrier_Sync(void)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);
}

static void setup(int argc, char *argv[], struct ocfs2_move_extents *range)
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

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	memset(range, 0, sizeof(*range));

	if (parse_opts(argc, argv, range))
		usage();

	srand(getpid());

	MPI_Barrier_Sync();

	return;
}

static void teardown(int ret)
{
	if (ret == MPI_RET_SUCCESS) {

		MPI_Finalize();
		exit(0);

	} else {

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

unsigned long get_rand_ul(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

int open_file(const char *filename, int flags)
{
	int fd, ret = 0;

	fd = open64(filename, flags, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		abort_printf("open file %s failed:%d:%s\n", filename,
			     ret, strerror(ret));
		return -1;
	}

	return fd;
}

int get_i_size(char *filename, unsigned long *size)
{
	struct stat stat;
	int ret = 0, fd;

	fd = open_file(filename, O_RDONLY);
	if (fd)
		return fd;

	ret = fstat(fd, &stat);
	if (ret == -1) {
		ret = errno;
		abort_printf("stat failure %d: %s\n", ret, strerror(ret));
		return ret;
	}

	*size = (unsigned long) stat.st_size;
	return ret;
}

static int defrag_file(char *file, struct ocfs2_move_extents *range)
{
	int fd = -1, ret = 0;

	range->me_flags |= OCFS2_MOVE_EXT_FL_AUTO_DEFRAG;

	fd = open(file, O_RDWR);
	if (fd < 0) {
		ret = errno;
		abort_printf("open file %s failed %d %s\n", file, ret,
			     strerror(ret));
		ret = fd;
		goto out;
	}

	ret = ioctl(fd, OCFS2_IOC_MOVE_EXT, range);
	if (ret < 0) {
		ret = errno;
		abort_printf("ioctl failed %d %s\n", ret, strerror(ret));
		goto out;
	}

	if (!(range->me_flags & OCFS2_MOVE_EXT_FL_COMPLETE))
		abort_printf("defrag didn't get finished completely.\n");
out:
	return ret;
}

static int defrag_files(char *workdir, struct ocfs2_move_extents *range,
			int concurrent, int verbose, int random)
{
	DIR *dir;
	int i = 0, ret = 0;
	struct dirent *dirent;
	char path[PATH_MAX];
	unsigned long i_size = 0;

	dir = opendir(workdir);
	if (!dir) {
		ret = errno;
		abort_printf("opendir failure %d: %s\n", ret, strerror(ret));
		goto out;
	}

	while ((dirent = readdir(dir))) {
		if (concurrent)
			goto do_defrag;

		if ((i++ % size) != rank)
			continue;
do_defrag:
		snprintf(path, PATH_MAX, "%s/%s", workdir, dirent->d_name);
		if (random) {
			ret = get_i_size(path, &i_size);
			if (ret)
				goto out;
			range->me_start = get_rand_ul(0, range->me_start );
			range->me_len =
				get_rand_ul(range->me_threshold, i_size);
		}

		if (verbose)
			rank_printf("defrag %s at offset: %llu, len: %llu, "
				    "threshold: %llu\n", path, range->me_start,
				    range->me_len, range->me_threshold);

		ret = defrag_file(path, range);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int run_test(struct ocfs2_move_extents *range)
{
	int ret = 0;
	
	ret = defrag_files(workplace, range, 0, verbose, do_random);
	if (ret)
		goto out;

	MPI_Barrier_Sync();
out:
	should_exit(ret);
	return ret;	
}

int main(int argc, char **argv)
{
	struct ocfs2_move_extents range;

	setup(argc, argv, &range);

	run_test(&range);

	teardown(MPI_RET_SUCCESS);

	return 0;
}
