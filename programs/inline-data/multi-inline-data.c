/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi-inline-data.c
 *
 * A MPI compatible program to perform following test for inline-data
 * among multiple nodes concurrently,
 *
 * Verify I/O to/from files small enough to hold inline data. Includes
 * some tests intended to force an inode out to an extent list,test will be *
 * [I] Tests of inline-data code. All tests read back the pattern
 *     written to verify data integrity.
 *    1) Write a pattern, read it back.
 *    2) Shrink file by some bytes
 *    3) Extend file again
 *    4) Mmap file and read pattern
 *    5) Reserve space inside of region
 *    6) Punch hole in the middle
 *
 * [II] Tests intended to force data out of inode. Before each test,
 *      we'll have to truncate to zero and re-write file.
 *    1) Seek past EOF, write pattern.
 *    2) Mmap and do a write inside of i_size
 *    3) Extend file past min size
 *    4) Resrve space past min size
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

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <ocfs2/ocfs2.h>

#include <sys/ioctl.h>
#include <inttypes.h>
#include <linux/types.h>

#include <mpi.h>

#define PATTERN_SZ		8192
#define HOSTNAME_MAX_SZ		255
#define OCFS2_MAX_FILENAME_LEN	255

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define WORK_PLACE	"inline-data-test"

static char *prog;
static char device[100];
static char uuid[100];

ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;

char *pattern;
unsigned int max_inline_size;
char *read_buf;

unsigned long page_size;
unsigned int blocksize = 4096;
unsigned long clustersize;

unsigned int id_count;
unsigned long i_size;

char mount_point[OCFS2_MAX_FILENAME_LEN];
char work_place[OCFS2_MAX_FILENAME_LEN];
char file_name[OCFS2_MAX_FILENAME_LEN];
static char dirent[OCFS2_MAX_FILENAME_LEN];

static int iteration = 1;

static int rank = -1, size;
static char hostname[HOSTNAME_MAX_SZ];

extern unsigned long get_rand(unsigned long min, unsigned long max);
extern inline char rand_char(void);
extern void fill_pattern(int size);
extern int truncate_pattern(int fd, unsigned int old_size,
			    unsigned int new_size);
extern int extend_pattern(int fd, unsigned int old_size, unsigned int new_size);
extern int try_ioctl(int which, int fd, unsigned int offset,
		     unsigned int count);
extern int try_reserve(int fd, unsigned int offset, unsigned int count);
extern int try_punch_hole(int fd, unsigned int offset, unsigned int count);
extern int mmap_write_at(int fd, const char *buf, size_t count, off_t offset);
extern int write_at(int fd, const void *buf, size_t count, off_t offset);
extern int prep_file_no_fill(unsigned int size, int open_direct);
extern int prep_file(unsigned int size);
extern int verify_pattern(int size, char *buf);
extern int __verify_pattern_fd(int fd, unsigned int size, int direct_read);
extern int verify_pattern_fd(int fd, unsigned int size);
extern int verify_pattern_mmap(int fd, unsigned int size);
extern int uuid2dev(const char *uuid, char *dev);
extern int open_ocfs2_volume(char *device_name);
extern int is_file_inlined(char *dirent_name, unsigned long *i_size,
			   unsigned int *id_count);

static void usage(void)
{
	printf("Usage: multi-inline-data [-i <iteration>] "
	       "<-u <uuid>> <mount_point>\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "files/dirs with inline data.\n\n"
	       "iteration specify the running times.\n"
	       "uuid and mount_point are mandatory.\n");

	MPI_Finalize();

	exit(1);
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

static int parse_opts(int argc, char **argv)
{
	int c;
	while (1) {
		c = getopt(argc, argv, "U:u:I:i:");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
		case 'I':
			iteration = atol(optarg);
			break;
		case 'u':
		case 'U':
			strcpy(uuid, optarg);
			break;
		default:
			break;
		}
	}

	if (strcmp(uuid, "") == 0)
		return EINVAL;

	if (argc - optind != 1)
		return EINVAL;

	strcpy(mount_point, argv[optind]);
	if (mount_point[strlen(mount_point) - 1] == '/')
		 mount_point[strlen(mount_point) - 1] = '\0';

	memset(device, 0, 100);

	if (uuid2dev(uuid, device))
		abort_printf("Failed to get device name from uuid!\n");

	return 0;

}

static int should_inlined_or_not(int is_inlined, int should_inlined,
				 int test_no)
{
	/* is_inlined represent if the ret is inlined or not
	   while should_inlined represnt if we expect it inlined or not
	*/
	if (should_inlined) {
		if (!is_inlined) {
			fprintf(stderr, "After Test #%d, file %s should be "
				"inlined here!\n", test_no, file_name);
			fprintf(stderr, "File(%s): i_size = %d,id_count = %d\n",
				file_name, i_size, id_count);
			abort_printf("A extented file detected!\n");
		}

	} else {
		if (is_inlined) {
			fprintf(stderr, "After Test #%d, file %s should be "
				"extented here!\n", test_no, file_name);
			fprintf(stderr, "File(%s): i_size = %d,id_count = %d\n",
				file_name, i_size, id_count);
			abort_printf("A inlined file detected!\n");

		}
	}

	return 0;
}


static int setup(int argc, char *argv[])
{
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Init Failed!\n");

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0)
		abort_printf("get hostname failed!\n");

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

	if (parse_opts(argc, argv))
		usage();

	ret = open_ocfs2_volume(device);
	if (ret < 0)
		abort_printf("open ocfs2 volume failed!\n");

	ret = posix_memalign((void *)&pattern, blocksize, PATTERN_SZ);
	if (ret) {
		fprintf(stderr, "posix_memalign error %d: \"%s\"\n",
			ret, strerror(ret));
		return 1;
	}

	ret = posix_memalign((void *)&read_buf, blocksize, PATTERN_SZ);
	if (ret) {
		fprintf(stderr, "posix_memalign error %d: \"%s\"\n",
		ret, strerror(ret));
		return 1;
	}

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);
	snprintf(work_place, OCFS2_MAX_FILENAME_LEN, "%s/%s", mount_point,
		 WORK_PLACE);

	if (rank == 0) { /*rank 0 setup the testing env*/
		mkdir(work_place, FILE_MODE);
		root_printf("BlockSize:\t\t%d\nMax Inline Data Size:\t%d\n"
			    "ClusterSize:\t\t%d\nPageSize:\t\t%d\nWorkingPlace:"
			    "\t\t%s\n\n", blocksize, max_inline_size,
			    clustersize, page_size, work_place);
	}

	return 0;
}

static int teardown(void)
{
	if (rank == 0)
		rmdir(work_place);

	MPI_Finalize();

	return 0;
}

static void send_pattern_to_ranks(void)
{
	int i, ret;

	MPI_Request request;
	MPI_Status status;

	if (rank == 0) {
		for (i = 1; i < size; i++) {
			ret = MPI_Isend(pattern, PATTERN_SZ, MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Isend failed!\n");
			MPI_Wait(&request, &status);
		}

	} else {
		ret = MPI_Isend(pattern, PATTERN_SZ, MPI_BYTE, 0, 1,
				MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Isend failed!\n");
		MPI_Wait(&request, &status);
	}

}

static void recv_pattern_from_ranks(void)
{
	int i, ret;

	MPI_Request request;
	MPI_Status status;

	if (rank == 0) {
		for (i = 1; i < size; i++) {
			ret = MPI_Irecv(pattern, PATTERN_SZ, MPI_BYTE, i, 1,
					MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Irecv failed!\n");
			MPI_Wait(&request, &status);
		}

	} else {
		ret = MPI_Irecv(pattern, PATTERN_SZ, MPI_BYTE, 0, 1,
				MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Irecv failed!\n");
		MPI_Wait(&request, &status);
	}

}


static int test_regular_file(int test_no)
{
	int ret, fd, new_size, old_size, init_count, count, j, offset;
	unsigned int test_num = 1;
	int hole_gap = 2;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	snprintf(dirent, OCFS2_MAX_FILENAME_LEN,
		 "multiple-inline-data-test-file");
	snprintf(file_name, OCFS2_MAX_FILENAME_LEN, "%s/%s", work_place,
		 dirent);

	root_printf("################Test Round %d :%s################\n",
		    test_no, file_name);

	root_printf("Test  %d: Write basic pattern\n", test_num);

	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep_file failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();
	}

	/*wait completion of file creation by rank 0*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		abort_printf("pattern verification failed!\n");
	if (rank != 0)
		close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Rewrite portion of basic pattern\n", test_num);
	offset = get_rand(0, max_inline_size - 1);
	count = get_rand(1, max_inline_size - offset);
	if (rank != 0) { /*none-root rank perfrom cocurrent write*/
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = write_at(fd, pattern + offset, count, offset);
		if (ret)
			abort_printf("write_at failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);

	if (rank == 0) { /*rank 0 verify the pattern*/
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			abort_printf("pattern verification failed!\n");
	}
	if (rank != 0)
		close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Reduce size with truncate\n", test_num);
	old_size = max_inline_size;
	new_size = max_inline_size / 2;
	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = truncate_pattern(fd, old_size, new_size);
		if (ret)
			abort_printf("truncate pattern failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();

	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			abort_printf("pattern verification failed!\n");
	}
	if (rank != 0)
		close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Extend file\n", test_num);
	old_size = max_inline_size / 2;
	new_size = max_inline_size;
	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = extend_pattern(fd, old_size, new_size);
		if (ret)
			abort_printf("extend pattern failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			abort_printf("pattern verification failed!\n");
	}
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Double write, both extending\n", test_num);
	count = max_inline_size / 2;
	init_count = count / 2;
	if (rank == 0) {
		fill_pattern(count);
		fd = prep_file_no_fill(init_count, 0);
		if (fd < 0)
			abort_printf("prep_file_no_fill failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		offset = get_rand(1, init_count);
		ret = write_at(fd, pattern + offset, count - offset, offset);
		if (ret)
			abort_printf("write_at failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, count);
		if (ret)
			abort_printf("verify pattern failed!\n");
	}
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Fsync\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {

		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = fsync(fd);
		if (ret)
			abort_printf("fsync failed!\n");
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			abort_printf("verify pattern failed!\n");

	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		abort_printf("verify pattern failed!\n");
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Fdatasync\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = fdatasync(fd);
		if (ret)
			abort_printf("fdataasync failed!\n");
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		abort_printf("verify pattern failed!\n");
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Mmap reads\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		for (j = 1; j <= max_inline_size; j++) {
			ret = verify_pattern_mmap(fd, j);
			if (ret)
				abort_printf("verify mmap pattern failed!\n");
		}
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	if (rank != 0)
		close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Reserve space\n", test_num);
	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		offset = get_rand(0, max_inline_size - 1);
		count = get_rand(1, max_inline_size - offset);
		ret = try_reserve(fd, offset, count);
		if (ret != ENOTTY) {
			if (ret)
				abort_printf("try reserve failed!\n");

			ret = verify_pattern_fd(fd, max_inline_size);
			if (ret)
				abort_printf("verify pattern failed!\n");
			}
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Punch hole\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		offset = get_rand(0, max_inline_size - 1);
		count = get_rand(1, max_inline_size - offset);
		ret = try_punch_hole(fd, offset, count);
		if (ret != ENOTTY) {
			if (ret)
				abort_printf("try punch hole failed!\n");
		}
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Force expansion to extents via large write\n",
		    test_num);
	if (rank == 0) {
		fill_pattern(PATTERN_SZ);
		fd = prep_file_no_fill(max_inline_size, 0);
		if (fd < 0)
			abort_printf("prep file no fill failed!\n");
		send_pattern_to_ranks();

	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	count = PATTERN_SZ - max_inline_size;
	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = write_at(fd, &pattern[max_inline_size], count,
			       max_inline_size);
		if (ret)
			abort_printf("write at failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 0, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, PATTERN_SZ);
		if (ret)
			abort_printf("verfiy pattern failed!\n");
	}
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Force expansion to extents via mmap write\n",
		    test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		fill_pattern(max_inline_size);
		ret = mmap_write_at(fd, pattern, max_inline_size, 0);
		if (ret)
			abort_printf("mmap write failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			abort_printf("verify pattern failed!\n");
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 0, test_num);
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Force expansion to extents via large extend\n",
		    test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	old_size = max_inline_size;
	new_size = 2 * max_inline_size;

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = extend_pattern(fd, old_size, new_size);
		if (ret)
			abort_printf("extend pattern failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 0, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			abort_printf("verify pattern failed!\n");
	}
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Force expansion to extents via large"
		    " reservation\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	new_size = PATTERN_SZ;
	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = try_reserve(fd, 0, new_size);
		if (ret != ENOTTY) {
			if (ret)
				abort_printf("try reserve failed!\n");
			ret = extend_pattern(fd, max_inline_size, new_size);
			if (ret)
				abort_printf("extend pattern failed!\n");
			send_pattern_to_ranks();
		}
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 0, test_num);
	if (rank == 0) {
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			abort_printf("verify pattern failed!\n");
	}
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: O_DIRECT read\n", test_num);
	if (rank == 0) {
		fd = prep_file(max_inline_size);
		if (fd < 0)
			abort_printf("prep file failed!\n");
		close(fd);
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR|O_DIRECT);
		if (fd < 0)
			abort_printf("open file failed with direct mode!\n");
		ret = __verify_pattern_fd(fd, max_inline_size, 1);
		if (ret)
			abort_printf("_verify pattern failed!\n");
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);
	close(fd);
	test_num++;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: O_DIRECT write\n", test_num);
	if (rank == 0) {
		fill_pattern(max_inline_size);
		fd = prep_file_no_fill(max_inline_size, 1);
		if (fd < 0)
			abort_printf("prep file no fill failed!\n");
		send_pattern_to_ranks();
	} else {
		recv_pattern_from_ranks();
	}

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank != 0) {
		fd = open(file_name, O_RDWR, FILE_MODE);
		if (fd < 0)
			abort_printf("open file failed!\n");
		ret = __verify_pattern_fd(fd, max_inline_size, 1);
		if (ret)
			abort_printf("__verify pattern failed!\n");
		close(fd);
	}
	/*
	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 0, test_num);
	*/
	test_num++;
	close(fd);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	if (rank == 0)
		unlink(file_name);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	root_printf("Test  %d: Multiple files\n", test_num);
	snprintf(dirent, OCFS2_MAX_FILENAME_LEN,
		 "multiple-inline-data-test-file-%d", rank);
	snprintf(file_name, OCFS2_MAX_FILENAME_LEN, "%s/%s", work_place,
		 dirent);
	fd = prep_file(max_inline_size);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	offset = get_rand(0, max_inline_size - 1);
	count = get_rand(1, max_inline_size - offset);
	ret = write_at(fd, pattern + offset, count, offset);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	ret = verify_pattern_fd(fd, max_inline_size);

	ret = is_file_inlined(dirent, &i_size, &id_count);
	should_inlined_or_not(ret, 1, test_num);

	close(fd);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	unlink(file_name);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);


bail:
	root_printf("All File I/O Tests Passed\n");

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Barrier failed: %d\n", ret);

	return ret;
}
int main(int argc, char **argv)
{
	int ret;
	int i;

	ret = setup(argc, argv);
	if (ret)
		abort_printf("setup error!\n");

	for (i = 0; i < iteration; i++) {
		ret = test_regular_file(i);
		if (ret)
			abort_printf("test_regular_file error!\n");
	}

	teardown();

	return 0;
}
