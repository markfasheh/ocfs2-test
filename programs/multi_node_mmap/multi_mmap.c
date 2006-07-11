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
static char *filename;
static int prep_open_flags = O_CREAT|O_TRUNC;
static int mmap_reads = 1;
static int mmap_writes = 1;
static int random_readers = 0;
static int random_writers = 0;
static int pre_populate = 0;
static int zero_with_hole = 0;
static unsigned int inject_truncate = 0;
static unsigned int injection_pass;
static unsigned int blocksize = 508;
static unsigned int max_passes = 1;
static char startchar = 'a';
static unsigned int num_blocks;

static int this_pass;
static char *local_pattern;
static char *tmpblock;
static char *mapped_area = NULL;

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void fill_with_expected_pattern(char *buf, unsigned int block)
{
	int i;
	char c = '\0';

	c = startchar + block;

	for(i = 0; i < blocksize; i++)
		buf[i] = c;
}

static void usage(void)
{
	printf("mmap_test [-t] [-c] [-r <how>] [-w <how>] [-b <blocksize>] "
	       "[-h] [-i <iter>] <filename>\n\n"
       "Requires at least two processes. The rank zero process preps\n"
       "a file by opening it O_CREAT|O_TRUNC and filling the file\n"
       "with a pattern. All nodes then open the file and\n"
       "mmap(MAP_SHARED|PROT_READ|PROT_WRITE) it's entire length.\n"
       "In a round robin fashion, a node is picked to write a\n"
       "pattern into their pre-determined block and the other nodes\n"
       "read the whole file to verify the expected pattern.\n\n"
       "OPTIONS:\n"
       "-t\t\tDon't create or trunc the file - will fail if it doesn't exist\n"
       "-c\t\tPopulate the local cache by reading the full file first\n"
       "-r <how>\tReaders use \"mmap\", \"regular\" or \"random\" I/O: default \"mmap\"\n"
       "-w <how>\tWriters use \"mmap\", \"regular\" or \"random\" I/O: default \"mmap\"\n"
       "-b <blocksize>\tBlocksize to use, defaults to 508. Must be > 10\n"
       "\t\tUse unaligned block sizes for best results.\n"
       "-h\t\tNormally the file is zeroed via eof writes, but this instructs\n"
       "\t\tthe code to only ftruncate(), thus creating a hole where it\n"
       "\t\twill be writing\n"
       "-i <iter>\tNumber of times to pass through the file. Default is 1\n"
       "-e <which>\tHave the rank zero node inject an error by truncating\n"
       "\t\tthe entire file length on iteration <which>\n");

	MPI_Finalize();
	exit(1);
}

static int set_rw_arg(const char *arg, int *use_mmap, int *random)
{
	if (strcmp("regular", optarg) == 0)
		*use_mmap = 0;
	else if (strcmp("mmap", optarg) == 0)
		*use_mmap = 1;
	else if (strcmp("random", optarg) == 0)
		*random = 1;
	else
		return EINVAL;
	return 0;
}

static int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "ctb:r:w:hi:e:");
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			pre_populate = 1;
			break;
		case 't':
			prep_open_flags = 0;
			break;
		case 'b':
			blocksize = atoi(optarg);
			if (blocksize <= 10)
				return EINVAL;
			break;
		case 'r':
			if (set_rw_arg(optarg, &mmap_reads, &random_readers))
				return EINVAL;
			break;
		case 'w':
			if (set_rw_arg(optarg, &mmap_writes, &random_writers))
				return EINVAL;
			break;
		case 'h':
			zero_with_hole = 1;
			break;
		case 'i':
			max_passes = atoi(optarg);
			break;
		case 'e':
			inject_truncate = 1;
			injection_pass = atoi(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (zero_with_hole && !prep_open_flags) {
		fprintf(stderr, "Mixing '-t' and -h' flags is not allowed!\n\n");
		return EINVAL;
	}

	if (inject_truncate && (injection_pass > max_passes))
		return EINVAL;

	if (argc - optind != 1)
		return EINVAL;

	filename = argv[optind];

	return 0;
}

static void end_test(int fd)
{
	int ret;

	ret = munmap(mapped_area, num_blocks*blocksize);
	if (ret) {
		ret = errno;
		abort_printf("Error %d unmapping area: %s\n",
			     ret, strerror(ret));
	}

	close(fd);
}

static void read_block(int fd, unsigned int blkno, char *block)
{
	int ret;
	unsigned int offset = blkno * blocksize;
	char *start = mapped_area + offset;

	if (mmap_reads) {
		memcpy(block, start, blocksize);
	} else {
		if (lseek(fd, offset, SEEK_SET) == ((off_t) - 1)) {
			ret = errno;
			abort_printf("Error %d seeking to %u: %s\n",
				     ret, offset, strerror(ret));
		}
		ret = read(fd, block, blocksize);
		if (ret == -1) {
			ret = errno;
			abort_printf("Error %d reading file: %s",
				     ret, strerror(ret));
		}
		if (ret < blocksize)
			abort_printf("Short read! requested %u, recieved %u\n",
				     blocksize, ret);
	}
}

static void write_block(int fd, unsigned int blkno, char *block)
{
	int ret;
	unsigned int offset = blkno * blocksize;
	char *start = mapped_area + offset;

	if (mmap_writes) {
		memcpy(start, block, blocksize);
	} else {
		if (lseek(fd, offset, SEEK_SET) == ((off_t) - 1)) {
			ret = errno;
			abort_printf("Error %d seeking to %u: %s\n",
				     ret, offset, strerror(ret));
		}
		ret = write(fd, block, blocksize);
		if (ret == -1) {
			ret = errno;
			abort_printf("Error %d reading file: %s",
				     ret, strerror(ret));
		}
		if (ret < blocksize)
			abort_printf("Short write! requested %u, recieved %u\n",
				     blocksize, ret);
	}
}

static void do_mmap(int fd, off_t length)
{
	int ret;

	mapped_area = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_SHARED, fd,
			   0);
	if (mapped_area == MAP_FAILED) {
		ret = errno;
		abort_printf("Error %d mapping file \"%s\" from offset 0 \n"
			     "for length %lu: %s\n",
			     ret, filename, (unsigned long)length,
			     strerror(ret));
	}
}

static void zero_file(int fd, off_t length)
{
	char *block = calloc(1, blocksize);
	int i, ret;

	if (!block)
		abort_printf("No memory left!|n");

	if (zero_with_hole) {
		ret = ftruncate(fd, length);
		if (ret == -1) {
			ret = errno;
			abort_printf("Error %d truncating \"%s\" to %lu: %s\n",
				     ret, filename, (unsigned long) length,
				     strerror(ret));
		}

		return;
	}

	for(i = 0; i < num_blocks; i++) {
		ret = write(fd, block, blocksize);
		if (ret != blocksize)
			abort_printf("Write returns %d, expected %d\n",
				     ret, blocksize);
	}
}

static int prep_file(void)
{
	int ret, fd, flags = O_RDWR;
	off_t length = blocksize*num_blocks;

	if (rank)
		goto open_after;

	flags |= prep_open_flags;

	fd = open(filename, flags, S_IROTH|S_IRGRP|S_IRUSR|S_IWUSR);
	if (fd == -1) {
		ret = errno;
		abort_printf("Error %d opening file \"%s\": %s\n",
			     ret, filename, strerror(ret));
	}

	do_mmap(fd, length);

	zero_file(fd, length);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	goto populate;

open_after:
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Prep MPI_Barrier failed: %d\n", ret);

	fd = open(filename, flags);
	if (fd == -1) {
		ret = errno;
		abort_printf("Error %d opening file \"%s\": %s\n",
			     ret, filename, strerror(ret));
	}

	do_mmap(fd, length);

populate:
	if (pre_populate) {
		int i;
		char *b = mapped_area;

		for(i = 0; i < num_blocks; i++) {
			memcpy(local_pattern, b, blocksize);
			b += blocksize;
		}
	}

	return fd;
}

static int randomize_bool(void)
{
	if (rand() < (RAND_MAX / 2))
		return 0;
	return 1;
}

static void trunc_file(int fd)
{
	int ret;

	printf("%s (rank %d): Injecting an error by truncating to zero.\n",
	       hostname, rank);

	ret = ftruncate(fd, 0);
	if (ret == -1) {
		ret = errno;
		abort_printf("Error %d truncating \"%s\" to 0: %s\n",
			     ret, filename, strerror(ret));
	}
}

static void do_writer(int fd, unsigned int block, char *pattern)
{
	int ret;

	if (random_writers)
		mmap_writes = randomize_bool();

	printf("%s (rank %d): Write  via %s block %u of '%c'\n",
	       hostname, rank, mmap_writes ? "mmap " : "write", block,
	       pattern[0]);

	fflush(stdout);

	write_block(fd, block, pattern);

	if (inject_truncate && (rank == 0) &&
	    (this_pass == injection_pass))
		trunc_file(fd);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Write MPI_Barrier failed: %d\n", ret);
}

static void do_reader(int fd, unsigned int block, char *pattern)
{
	int ret;

	if (random_readers)
		mmap_reads = randomize_bool();

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Verify MPI_Barrier failed: %d\n", ret);

	printf("%s (rank %d): Expect via %s block %u of '%c'\n",
	       hostname, rank, mmap_reads ? "mmap " : "read ", block,
	       pattern[0]);

	fflush(stdout);

	read_block(fd, block, tmpblock);

	if (memcmp(pattern, tmpblock, blocksize)) {
		fprintf(stderr,
			"%s (rank %d): Verification failed for block %u.\n",
			hostname, rank, block);
		fprintf(stderr,
			"%s (rank %d): First 10 expected chars: \"%.*s\"\n",
			hostname, rank, 10, pattern);
		fprintf(stderr,
			"%s (rank %d): First 10 actual chars: \"%.*s\"\n",
			hostname, rank, 10, tmpblock);

		fflush(stderr);

		/* This sleep is here so that other processes who may
		 * also be about to fail can do so before we abort the
		 * mpi instance. */
		sleep(10);

		MPI_Abort(MPI_COMM_WORLD, 1);
	}
}

static void write_verify_blocks(int fd)
{
	int i;
	unsigned int block;
	char *buf = mapped_area + (block*blocksize);

	for(i = 0; i < num_blocks; i++) {
		block = i;

		buf = mapped_area + (block * blocksize);
		fill_with_expected_pattern(local_pattern, block);

		if ((i % num_procs) == rank)
			do_writer(fd, block, local_pattern);
		else
			do_reader(fd, block, local_pattern);
	}
}

int main(int argc, char *argv[])
{
	int ret, fd;

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

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

	if (num_procs > 20 || num_procs < 2)
		abort_printf("Process count should be between 2 and 20\n");

        printf("%s: rank: %d, procs: %d, filename \"%s\"\n",
	       hostname, rank, num_procs, filename);

	num_blocks = num_procs;

	local_pattern = calloc(1, blocksize);
	tmpblock = calloc(1, blocksize);
	if (!local_pattern || !tmpblock)
		abort_printf("No memory to allocate pattern!\n");

	srand(getpid());

	fd = prep_file();

	for (this_pass = 0; this_pass < max_passes; this_pass++) {
		write_verify_blocks(fd);

		if ((startchar + num_procs) > 'z')
			startchar = 'a';
		else
			startchar++;
	}

	end_test(fd);

        MPI_Finalize();

	return 0;
}
