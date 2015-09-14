/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * multi_reflink_test.c
 *
 * A mpi compatible program to test reflinks on ocfs2
 * concurently among multiple nodes.
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

#include "reflink_test.h"
#include "xattr_test.h"

#include <mpi.h>

ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;

unsigned int blocksize;
unsigned long clustersize;
unsigned int max_inline_size;

int open_rw_flags = FILE_RW_FLAGS;
int open_ro_flags = FILE_RO_FLAGS;

unsigned long page_size;
unsigned long file_size = 1024 * 1024;

char *prog;

static char workplace[PATH_MAX];
static char orig_path[PATH_MAX];
static char ref_path[PATH_MAX];
static char hostname[HOSTNAME_MAX_SZ];

static int iteration = 1;
static int testno = 1;

static int rank = -1, size;

static unsigned long ref_counts = 10;
static unsigned long ref_trees = 10;

int test_flags = 0x00000000;

static char dio_buf[DIRECTIO_SLICE] __attribute__ ((aligned(DIRECTIO_SLICE)));

/*
  used to verify if original file corrupt
*/

char *orig_pattern;

/*
 * Here is the variable for xattr tests.
*/

char filename[PATH_MAX];

unsigned long xattr_nums = DEFAULT_XATTR_NUMS;
unsigned int xattr_name_sz = DEFAULT_XATTR_NAME_SZ;
unsigned long xattr_value_sz = DEFAULT_XATTR_VALUE_SZ;

char *xattr_name;
char *xattr_value;
char *xattr_value_get;
char *list;
char **xattr_name_list_set;
char **xattr_name_list_get;
char xattr_namespace_prefix[10];

char value_prefix_magic[] = "abcdefghijklmnopqrs";
char value_postfix_magic[] = "srqponmlkjihgfedcba";
char value_prefix_get[20];
char value_postfix_get[20];
char value_sz[6];
char value_sz_get[6];
char *name_get;

static unsigned long list_sz;

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
       root_printf("Usage: multi_reflink_test [-i iteration] [-l file_size] "
	       "[-p refcount_tree_pairs] [-n reflink_nums] <-w work_place> "
	       "[-f] [-x] [-r] [-m] [-y] [-s] [-c] [-O] [-A]\n"
	       "iteration specify the running times.\n"
	       "file_size specify the size of original file.\n"
	       "reflink_nums specify the number of reflinks.\n"
	       "workplace specify the directory where tests carried out.\n"
	       "refcount_tree_pairs specify the refcount tree numbers in fs.\n"
	       "-f specify the basic functional test.\n"
	       "-x specify the xattr combination test.\n"
	       "-r specify the random test.\n"
	       "-y specify the destructive test.\n"
	       "-s specify the stress test.\n"
	       "-c specify the comprehensive test.here need 6 ranks at least.\n"
	       "-O specify O_DIRECT test.\n"
	       "-A specify asynchronous io test.\n"
	       "-m specify the mmap test.\n");

	MPI_Finalize();
	exit(1);

}

int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "I:i:w:OAfFrRmMyYcCsSW:n:N:l:L:p:P:x:X:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
		case 'I':
			iteration = atol(optarg);
			break;
		case 'N':
		case 'n':
			ref_counts = atol(optarg);
			break;
		case 'p':
		case 'P':
			ref_trees = atol(optarg);
			break;
		case 'l':
		case 'L':
			file_size = atol(optarg);
			break;
		case 'w':
		case 'W':
			strcpy(workplace, optarg);
			break;
		case 'f':
		case 'F':
			test_flags |= BASC_TEST;
			break;
		case 'x':
		case 'X':
			test_flags |= XATR_TEST;
			xattr_nums = atol(optarg);
			break;
		case 'O':
			test_flags |= ODCT_TEST;
			break;
		case 'A':
			test_flags |= ASIO_TEST;
			break;
		case 'r':
		case 'R':
			test_flags |= RAND_TEST;
			break;
		case 'm':
		case 'M':
			test_flags |= MMAP_TEST;
			break;
		case 'y':
		case 'Y':
			test_flags |= DEST_TEST;
			break;
		case 'c':
		case 'C':
			test_flags |= COMP_TEST;
			break;
		case 's':
		case 'S':
			test_flags |= STRS_TEST;
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

static void setup(int argc, char *argv[])
{
	int ret;
	unsigned long i;

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

	if (parse_opts(argc, argv))
		usage();

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);

	if (test_flags & XATR_TEST) {

		xattr_name = (char *)malloc(XATTR_NAME_MAX_SZ + 1);
		name_get = (char *)malloc(XATTR_NAME_MAX_SZ + 1);
		xattr_value = (char *)malloc(XATTR_VALUE_MAX_SZ);
		xattr_value_get = (char *)malloc(XATTR_VALUE_MAX_SZ);
		xattr_name_list_set = (char **)malloc(sizeof(char *) *
						      xattr_nums);

		for (i = 0; i < xattr_nums; i++)
			xattr_name_list_set[i] = (char *)malloc(255 + 1);

		list_sz = (unsigned long)((XATTR_NAME_MAX_SZ + 1) * xattr_nums);
		list = (char *)malloc(list_sz);
		xattr_name_list_get = (char **)malloc(sizeof(char *) *
						      xattr_nums);
		for (i = 0; i < xattr_nums; i++)
			xattr_name_list_get[i] = (char *)malloc(255 + 1);

	}

	orig_pattern = (char *)malloc(PATTERN_SIZE);
	memset(orig_pattern, 0, PATTERN_SIZE);

	MPI_Barrier_Sync();

	return;
}

static void teardown(int ret)
{
	unsigned long j;

	free(orig_pattern);

	if (test_flags & XATR_TEST) {

		free((void *)xattr_name);
		free((void *)name_get);
		free((void *)xattr_value);
		free((void *)xattr_value_get);

		for (j = 0; j < xattr_nums; j++)
			free((void *)xattr_name_list_set[j]);

		free((void *)xattr_name_list_set);

		free((void *)list);

		for (j = 0; j < xattr_nums; j++)
			free((void *)xattr_name_list_get[j]);

		free((void *)xattr_name_list_get);
	}


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

static int basic_test(void)
{
	int ret = 0, fd;
	char dest[PATH_MAX];
	int sub_testno = 1;

	char *write_buf = NULL, *read_buf = NULL;

	unsigned long write_size = 0, read_size = 0;
	unsigned long append_size = 0, truncate_size = 0;
	unsigned long interval, offset = 0;

	write_buf = (char *)malloc(HUNK_SIZE * 2);
	read_buf = (char *)malloc(HUNK_SIZE * 2);

	root_printf("Test %d: Multi-nodes basic refcount test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/multi_original_basic_refile",
		 workplace);

	root_printf("  *SubTest %d:Prepare original inode %s.\n",
		    sub_testno++, orig_path);

	if (!rank) {

		ret = prep_orig_file(orig_path, file_size, 1);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to reflink the original to increment the
	* refcount concurrently.
	*/
	root_printf("  *SubTest %d:Reflinking inode %s among nodes.\n",
		    sub_testno++, orig_path);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	if (!rank) {
		ret = verify_orig_file(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to do cow to decrement the
	* refcount concurrently.
	*/

	root_printf("  *SubTest %d:Cowing reflinks among nodes.\n",
		    sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		interval = file_size / 100;
		offset = 0;
		while (offset < file_size) {
			if (test_flags & RAND_TEST)
				write_size = get_rand(1, M_SIZE * 2);
			else
				write_size = 1;

			if (offset + write_size > file_size)
				write_size = file_size - offset;

			get_rand_buf(write_buf, write_size);
			if (test_flags & MMAP_TEST)
				ret = mmap_write_at_file(dest, write_buf,
							 write_size, offset);
			else
				ret = write_at_file(dest, write_buf, write_size,
						    offset);

			if (ret)
				goto bail_free;

			if (test_flags & RAND_TEST)
				offset += write_size + get_rand(1, interval);
			else
				offset += write_size + interval;
		}
	}

	MPI_Barrier_Sync();

	if (!rank) {
		ret = verify_orig_file(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to read reflinks concurrently
	*/
	root_printf("  *SubTest %d:Reading reflinks among nodes.\n",
		    sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		interval = file_size / 100;
		offset = 0;
		while (offset < file_size) {
			if (test_flags & RAND_TEST)
				read_size = get_rand(1, M_SIZE * 2);
			else
				read_size = 1;

			if (offset + read_size > file_size)
				read_size = file_size - offset;

			if (test_flags & MMAP_TEST)
				ret = mmap_read_at_file(dest, read_buf,
							read_size, offset);
			else
				ret = read_at_file(dest, read_buf, read_size,
						   offset);

			if (ret)
				goto bail_free;

			if (test_flags & RAND_TEST)
				offset = offset + read_size +
					 get_rand(1, interval);
			else
				offset = offset + read_size + interval;
		}

	}

	MPI_Barrier_Sync();

	if (!rank) {
		ret = verify_orig_file(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	if (test_flags & MMAP_TEST)
		goto bail;

	/*
	* All ranks try to append reflinks concurrently
	*/

       root_printf("  *SubTest %d:Appending reflinks among nodes.\n",
		   sub_testno++);

	if (rank) {
		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		fd = open64(dest, open_rw_flags | O_APPEND);
		if (fd < 0) {
			if (write_buf)
				free(write_buf);

			if (read_buf)
				free(read_buf);
			fd = errno;
			abort_printf("open file %s failed:%d:%s\n",
				     dest, fd, strerror(fd));
		}

		if (test_flags & RAND_TEST)
			append_size = get_rand(1, HUNK_SIZE);
		else
			append_size = HUNK_SIZE;

		get_rand_buf(write_buf, append_size);

		ret = write(fd, write_buf, append_size);
		if (ret < 0) {
			if (write_buf)
				free(write_buf);

			if (read_buf)
				free(read_buf);
			ret = errno;
			abort_printf("write file %s failed:%d:%s\n",
				     dest, ret, strerror(ret));
		}

		close(fd);
	}

	MPI_Barrier_Sync();

	if (!rank) {
		ret = verify_orig_file(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to truncate reflinks concurrently
	*/

	root_printf("  *SubTest %d:Truncating reflinks among nodes.\n",
		    sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);

		if (test_flags & RAND_TEST)
			truncate_size = get_rand(0, file_size);
		else
			truncate_size = file_size / (rank + 1);

		ret = truncate(dest, truncate_size);
		if (ret < 0) {
			if (write_buf)
				free(write_buf);

			if (read_buf)
				free(read_buf);
			ret = errno;
			abort_printf("truncate file %s failed:%d:%s\n",
				     dest, ret, strerror(ret));
		}
	}

	MPI_Barrier_Sync();

	if (!rank) {
		ret = verify_orig_file(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

bail:
	if (rank) {

		ret = do_unlink(dest);
		if (ret)
			goto bail_free;

	} else {

		ret = do_unlink(orig_path);
		if (ret)
			goto bail_free;
	}

	MPI_Barrier_Sync();

bail_free:

	if (write_buf)
		free(write_buf);

	if (read_buf)
		free(read_buf);
	
	should_exit(ret);

	return ret;
}

static int directio_test(void)
{
	int ret, fd;
	char dest[PATH_MAX];
	int sub_testno = 1;
	int o_flags_rw, o_flags_ro;

	unsigned long write_size = 0, read_size = 0;
	unsigned long append_size = 0, truncate_size = 0;
	unsigned long interval, offset = 0;

	unsigned long align_slice = 512;
	unsigned long align_filesz = align_slice;

	o_flags_rw = open_rw_flags;
	o_flags_ro = open_ro_flags;

	open_rw_flags |= O_DIRECT;
	open_ro_flags |= O_DIRECT;

	while (align_filesz < file_size)
		align_filesz += align_slice;

	root_printf("Test %d: Multi-nodes O_DIRECT test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/multi_original_directio_refile",
		 workplace);

	root_printf("  *SubTest %d:Prepare original inode %s.\n",
		    sub_testno++, orig_path);

	if (!rank) {

		ret = prep_orig_file_dio(orig_path, align_filesz);
		should_exit(ret);
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to reflink the original to increment the
	* refcount concurrently.
	*/
	root_printf("  *SubTest %d:Reflinking inode %s among nodes.\n",
		    sub_testno++, orig_path);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		ret = reflink(orig_path, dest, 1);
		should_exit(ret);
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to do cow to decrement the
	* refcount concurrently.
	*/

	root_printf("  *SubTest %d:Cowing reflinks by O_DIRECT writes among"
		    " nodes.\n", sub_testno++);
	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		interval = DIRECTIO_SLICE;
		offset = 0;

		while (offset < align_filesz) {

			write_size = DIRECTIO_SLICE;

			if (offset + write_size > align_filesz)
				write_size = align_filesz - offset;

			get_rand_buf(dio_buf, write_size);

			ret = write_at_file(dest, dio_buf, write_size, offset);

			should_exit(ret);

			offset += write_size + interval;
		}
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to read reflinks concurrently
	*/
	root_printf("  *SubTest %d:O_DIRECT reading reflinks among nodes.\n",
		    sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		interval = DIRECTIO_SLICE;
		offset = 0;

		while (offset < align_filesz) {

			read_size = DIRECTIO_SLICE;

			if (offset + read_size > align_filesz)
				read_size = align_filesz - offset;

			ret = read_at_file(dest, dio_buf, read_size, offset);

			should_exit(ret);

			offset = offset + read_size + interval;
		}

	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to append reflinks concurrently
	*/

       root_printf("  *SubTest %d:Appending reflinks among nodes.\n",
		   sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);
		fd = open64(dest, open_rw_flags | O_APPEND);
		if (fd < 0) {
			fd = errno;
			abort_printf("open file %s failed:%d:%s\n",
				     dest, fd, strerror(fd));
		}

		append_size = DIRECTIO_SLICE;

		get_rand_buf(dio_buf, append_size);

		ret = write(fd, dio_buf, append_size);
		if (ret < 0) {
			ret = errno;
			abort_printf("write file %s failed:%d:%s\n",
				     dest, ret, strerror(ret));
		}

		close(fd);
	}

	MPI_Barrier_Sync();

	/*
	* All ranks try to truncate reflinks concurrently
	*/

	root_printf("  *SubTest %d:Truncating reflinks among nodes.\n",
		    sub_testno++);

	if (rank) {

		snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path, hostname, rank);

		truncate_size = get_rand(0, align_filesz / DIRECTIO_SLICE) *
					DIRECTIO_SLICE;

		ret = truncate(dest, truncate_size);

		if (ret < 0) {
			ret = errno;
			abort_printf("truncate file %s failed:%d:%s\n",
				     dest, ret, strerror(ret));
		}
	}

	MPI_Barrier_Sync();

	if (rank) {

		ret = do_unlink(dest);
		should_exit(ret);

	} else {

		ret = do_unlink(orig_path);
		should_exit(ret);
	}

	open_rw_flags = o_flags_rw;
	open_ro_flags = o_flags_ro;

	MPI_Barrier_Sync();

	return 0;
}

static int comp_test(void)
{

	int ret;
	char dest[PATH_MAX];

	unsigned long i;

	root_printf("Test %d: Multi-nodes comprehensive test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/multi_original_comp_refile",
		 workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);

	if (!rank) {
		ret = prep_orig_file(orig_path, file_size, 1);
		should_exit(ret);
		ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
		should_exit(ret);
		ret = reflink(orig_path, dest, 1);
		should_exit(ret);
	}

	MPI_Barrier_Sync();

	if (rank == 1) {
		/*also doing reflinks and unlinks*/
		printf("  *Test Rank %d: Doing reflinks,cows and unlink.\n",
		       rank);
		ret = do_reflinks(dest, dest, ref_counts, 0);
		should_exit(ret);
		ret = do_cows_on_write(dest, ref_counts, file_size, HUNK_SIZE);
		should_exit(ret);
		ret = do_unlinks(dest, ref_counts);
		should_exit(ret);
	}

	if (rank % 6 == 2) {
		/*Write former reflinks to cause cow*/
		printf("  *Test Rank %d: Doing cows.\n", rank);
		ret = do_cows_on_write(orig_path, ref_counts, file_size,
				       HUNK_SIZE);
		should_exit(ret);
	}

	if (rank % 6 == 3) {
		/*Read former reflinks*/
		printf("  *Test Rank %d: Doing reads.\n", rank);
		ret = do_reads_on_reflinks(orig_path, ref_counts, file_size,
					   HUNK_SIZE);
		should_exit(ret);
	}

	if (rank % 6 == 4) {
		/*Append to former reflinks*/
		printf("  *Test Rank %d: Doing appends.\n", rank);
		ret = do_appends(orig_path, ref_counts);
		should_exit(ret);
	}

	if (rank % 6 == 5) {
		/*Truncate former reflinks*/
		printf("  *Test Rank %d: Doing truncates.\n", rank);
		ret = do_cows_on_ftruncate(orig_path, ref_counts, file_size);
		should_exit(ret);
	}

	if (!rank) {

		printf("  *Test Rank %d: Doing verifications.\n", rank);
		for (i = 0; i < size; i++) {
			ret = verify_orig_file(orig_path);
			should_exit(ret);
			sleep(1);
		}
	}

	MPI_Barrier_Sync();

	if (!rank) {
		printf("  *Test Rank %d: Doing unlinks.\n", rank);
		ret = verify_orig_file(orig_path);
		should_exit(ret);
		ret = do_unlinks(orig_path, ref_counts);
		should_exit(ret);
		ret = do_unlink(orig_path);
		should_exit(ret);
		ret = do_unlink(dest);
		should_exit(ret);
	}

	MPI_Barrier_Sync();

	return 0;
}

static int do_xattr_cows(char *ref_pfx, unsigned long iter, int ea_nums)
{
	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret, o_ret;

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);

		fd = open64(dest, open_rw_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}
		strcpy(filename, dest);

		for (j = 0; j < ea_nums; j++) {

			strcpy(xattr_name, xattr_name_list_set[j]);
			xattr_value_sz = get_rand(1, XATTR_VALUE_MAX_SZ);

			if (xattr_value_sz > xattr_name_sz + 50)
				xattr_value_constructor(j);
			else
				xattr_value_generator(j, xattr_value_sz,
							xattr_value_sz);


			ret = add_or_update_ea(NORMAL, fd, XATTR_REPLACE,
					       "update");

			/* Probably some ranks have removed such entry*/
			if (ret < 0)
				ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE,
						       "add");

			if (ret < 0)
				continue;
		}

		close(fd);
	}

	return 0;
}

static int do_xattr_reads(char *ref_pfx, unsigned long iter, int ea_nums)
{
	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret, o_ret;

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);

		fd = open64(dest, open_ro_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}
		strcpy(filename, dest);

		for (j = 0; j < ea_nums; j++) {

			strcpy(xattr_name, xattr_name_list_set[j]);
			ret = read_ea(NORMAL, fd);

			if (ret < 0)
				continue;
		}

		close(fd);
	}

	return 0;
}

static int do_xattr_removes(char *ref_pfx, unsigned long iter, int ea_nums)
{

	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret, o_ret;

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);

		fd = open64(dest, open_rw_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}
		strcpy(filename, dest);

		for (j = 0; j < ea_nums; j++) {

			strcpy(xattr_name, xattr_name_list_set[j]);
			ret = remove_ea(NORMAL, fd);
			if (ret < 0)
				continue;

			if (get_rand(0, 1))
				continue;

			xattr_value_sz = get_rand(1, XATTR_VALUE_MAX_SZ);

			if (xattr_value_sz > xattr_name_sz + 50)
				xattr_value_constructor(j);
			else
				xattr_value_generator(j, xattr_value_sz,
						      xattr_value_sz);

			ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");

			if (ret < 0)
				ret = add_or_update_ea(NORMAL, fd,
						       XATTR_REPLACE, "update");

			if (ret < 0)
				continue;

		}

		close(fd);
	}

	return 0;
}

static int do_xattr_lists(char *ref_pfx, unsigned long iter)
{
	unsigned long i;
	char dest[PATH_MAX];

	int ret;

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);
		ret = verify_orig_file_xattr(NORMAL, dest, list_sz);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int do_xattr_data_cows(char *ref_pfx, unsigned long iter, int ea_nums)
{
	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret = 0, o_ret;

	unsigned long offset = 0, write_size = 0;
	char *write_buf = NULL;

	write_buf = (char *)malloc(HUNK_SIZE);

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);

		fd = open64(dest, open_rw_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, fd, strerror(fd));
			fd = o_ret;
			goto bail;
		}
		strcpy(filename, dest);

		for (j = 0; j < ea_nums; j++) {

			/* Update xattr*/
			strcpy(xattr_name, xattr_name_list_set[j]);
			xattr_value_sz = get_rand(1, XATTR_VALUE_MAX_SZ);

			if (xattr_value_sz > xattr_name_sz + 50)
				xattr_value_constructor(j);
			else
				xattr_value_generator(j, xattr_value_sz,
						      xattr_value_sz);

			ret = add_or_update_ea(NORMAL, fd, XATTR_REPLACE,
					       "update");
			if (ret < 0)
				ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE,
						       "add");
			if (ret < 0)
				continue;

			if (xattr_value_sz > xattr_name_sz + 50) {

				ret = read_ea(NORMAL, fd);
				if (ret < 0)
					goto bail;

				ret = xattr_value_validator(j);
				if (ret < 0)
					goto bail;
			}

			/* Update file data*/
			offset = get_rand(0, file_size - 1);

			write_size = get_rand(1, HUNK_SIZE);

			if (offset + write_size > file_size)
				write_size = file_size - offset;

			get_rand_buf(write_buf, write_size);

			ret = write_at(fd, write_buf, write_size, offset);
			if (ret < 0)
				goto bail;

		}

		close(fd);
	}

bail:
	if (write_buf)
		free(write_buf);

	return ret;
}

static int xattr_basic_test(int ea_name_size, int ea_value_size)
{
	int ret, fd = -1;
	int sub_testno = 1;
	char dest[PATH_MAX];

	MPI_Request request;
	MPI_Status  status;

	unsigned long i, j;

	xattr_name_sz = ea_name_size;
	xattr_value_sz = ea_value_size;


	snprintf(orig_path, PATH_MAX, "%s/multi_original_xattr_refile",
		 workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);

	root_printf("  *SubTest %d: Prep original inode.\n", sub_testno++);

	if (!rank) {

		ret = prep_orig_file(orig_path, file_size, 1);

		should_exit(ret);
	}

	MPI_Barrier_Sync();

	root_printf("  *SubTest %d: Prep %ld xattr name list among nodes.\n",
		    sub_testno++, xattr_nums);

	for (i = 0; i < xattr_nums; i++) {

		memset(xattr_name, 0, xattr_name_sz + 1);
		memset(xattr_value, 0, xattr_value_sz);
		memset(xattr_value_get, 0, xattr_value_sz);

		if (!rank) {

			xattr_name_generator(i, USER, xattr_name_sz,
					     xattr_name_sz);
			strcpy(xattr_name_list_set[i], xattr_name);

			for (j = 1; j < size; j++) {

				ret = MPI_Isend(xattr_name, xattr_name_sz + 1,
						MPI_BYTE, j, 1, MPI_COMM_WORLD,
						&request);
				if (ret != MPI_SUCCESS)
					abort_printf("MPI_Isend failed: %d\n",
						     ret);

				MPI_Wait(&request, &status);
			}

		} else {

			ret = MPI_Irecv(xattr_name, xattr_name_sz + 1, MPI_BYTE,
					0, 1, MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Irecv failed: %d\n", ret);

			MPI_Wait(&request, &status);
			strcpy(xattr_name_list_set[i], xattr_name);

		}

	}

	MPI_Barrier_Sync();

	root_printf("  *SubTest %d: Prep original inode with %ld EAs.\n",
		    sub_testno++, xattr_nums);

	if (!rank) {

		fd = open64(orig_path, open_rw_flags);

		for (i = 0; i < xattr_nums; i++) {

			strcpy(xattr_name, xattr_name_list_set[i]);
			xattr_value_constructor(i);
			ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
			should_exit(ret);
			ret = read_ea(NORMAL, fd);
			should_exit(ret);
			ret = xattr_value_validator(i);
			should_exit(ret);

		}

		ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
		should_exit(ret);
		ret = reflink(orig_path, dest, 1);
		should_exit(ret);

	}

	MPI_Barrier_Sync();

	if (rank % 6 == 1) {
		/*also doing reflinks and unlinks*/
		printf("  *SubTest Rank %d: Do reflinks and cows on %ld EAs.\n",
		       rank, xattr_nums);
		ret = do_reflinks(dest, dest, ref_counts, 0);
		should_exit(ret);
		ret = do_xattr_cows(dest, ref_counts, xattr_nums);
		should_exit(ret);
		ret = do_unlinks(dest, ref_counts);
		should_exit(ret);

	}

	if (rank % 6 == 2) {

		printf("  *SubTest Rank %d: Do cows on %ld EAs.\n", rank,
		       xattr_nums);
		ret = do_xattr_cows(orig_path, ref_counts, xattr_nums);
		should_exit(ret);
	}

	if (rank % 6 == 3) {

		printf("  *SubTest Rank %d: Do data&ea cows on %ld EAs.\n",
		       rank, xattr_nums);
		ret = do_xattr_data_cows(orig_path, ref_counts, xattr_nums);
		should_exit(ret);
	}

	if (rank % 6 == 4) {

		printf("  *SubTest Rank %d: Do reads on %ld EAs.\n", rank,
		       xattr_nums);
		xattr_value_sz = XATTR_VALUE_MAX_SZ;
		ret = do_xattr_reads(orig_path, ref_counts, xattr_nums);
		should_exit(ret);
	}

	if (rank % 6 == 5) {

		printf("  *SubTest Rank %d: Do lists on %ld EAs.\n", rank,
		       xattr_nums);
		if (list_sz < XATTR_LIST_MAX_SZ) {
			ret = do_xattr_lists(orig_path, ref_counts);
			should_exit(ret);
		}
	}

	MPI_Barrier_Sync();

	if (!rank) {

		printf("  *SubTest Rank %d: Do EA removal.\n", rank);

		ret = do_xattr_removes(orig_path, ref_counts, xattr_nums);
		should_exit(ret);

	}

	MPI_Barrier_Sync();

	if (!rank) {

		close(fd);
		ret = do_unlinks(orig_path, ref_counts);
		should_exit(ret);

		ret = do_unlink(dest);
		should_exit(ret);

		ret = do_unlink(orig_path);
		should_exit(ret);
	}

	return 0;
}

static int xattr_test(void)
{

	root_printf("Test %d: Multi-nodes basic xattr refcount test.\n",
		    testno++);
	xattr_basic_test(XATTR_NAME_LEAST_SZ, XATTR_NAME_LEAST_SZ + 62);

	root_printf("Test %d: Multi-nodes stress xattr refcount test..\n",
		    testno++);
	xattr_basic_test(XATTR_NAME_MAX_SZ, XATTR_VALUE_MAX_SZ);

	return 0;
}

static int stress_test(void)
{
	unsigned long i, j;
	int ret = 0, sub_testno = 1;
	char *write_buf = NULL, dest[PATH_MAX];
	char tmp_dest[PATH_MAX], tmp_orig[PATH_MAX];
	char *pattern_buf = NULL, *verify_buf = NULL;

	unsigned long offset = 0, write_size = 0, interval = 0;
	unsigned long verify_size = 0, verify_offset = 0;

	write_buf = (char *)malloc(HUNK_SIZE * 2);
	pattern_buf = (char *)malloc(HUNK_SIZE * 2);
	verify_buf = (char *)malloc(HUNK_SIZE * 2);

	root_printf("Test %d: Multi-nodes stress refcount test.\n", testno++);

	root_printf("  *SubTest %d: Stress test with tremendous refcount "
		    "trees.\n", sub_testno++);

	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/multi_original_stress_"
			 "refile_rank%d_%ld", workplace, rank, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = prep_orig_file(orig_path, 32 * 1024, 1);
		if (ret)
			goto bail;
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/multi_original_stress_refile_"
			 "rank%d_%ld", workplace, rank, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		offset = get_rand(0, 32 * 1024 - 1);
		write_size = 1;
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(dest, write_buf, write_size, offset);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {
		snprintf(orig_path, PATH_MAX, "%s/multi_original_stress_refile_"
			 "rank%d_%ld", workplace, rank, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = do_unlink(orig_path);
		if (ret)
			goto bail;
		ret = do_unlink(dest);
		if (ret)
			goto bail;
	}

	MPI_Barrier_Sync();

	root_printf("  *SubTest %d: Stress test with tremendous shared inodes "
		    "on one refcount tree.\n", sub_testno++);

	snprintf(orig_path, PATH_MAX, "%s/multi_original_stress_refile",
		 workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);

	if (!rank) {

		ret = prep_orig_file(orig_path, 10 * HUNK_SIZE, 1);
		if (ret)
			goto bail;

		for (i = 1; i < size; i++) {
			snprintf(ref_path, PATH_MAX, "%s_%ld", dest, i);
			ret = reflink(orig_path, ref_path, 1);
			if (ret)
				goto bail;
		}
	}

	MPI_Barrier_Sync();

	if (rank) {

		snprintf(ref_path, PATH_MAX, "%s_%d", dest, rank);
		ret = do_reflinks(ref_path, ref_path, ref_counts, 1);
		if (ret)
			goto bail;

		for (i = 0; i < ref_counts; i++) {

			if (get_rand(0, 1))
				continue;

			snprintf(ref_path, PATH_MAX, "%s_%dr%ld", dest, rank,
				 i);

			offset = get_rand(0, 10 * HUNK_SIZE - 1);

			write_size = get_rand(1, HUNK_SIZE);
			if (offset + write_size > 10 * HUNK_SIZE)
				write_size = 10 * HUNK_SIZE - offset;

			get_rand_buf(write_buf, write_size);
			ret = write_at_file(ref_path, write_buf, write_size,
					    offset);
			if (ret)
				goto bail;
		}

	}

	MPI_Barrier_Sync();

	if (rank) {
		for (i = 0; i < ref_counts; i++) {

			if (get_rand(0, 1))
				continue;

			snprintf(ref_path, PATH_MAX, "%s_%dr%ld", orig_path,
				 rank, i);

			if (truncate(ref_path, 0) < 0) {
				ret = errno;
				abort_printf("truncate refcount file %s failed: %d:%s\n",
					     ref_path, ret, strerror(ret));
				goto bail;
			}
		}
	}

	MPI_Barrier_Sync();

	if (rank) {

		snprintf(ref_path, PATH_MAX, "%s_%d", dest, rank);
		ret = do_unlinks(ref_path, ref_counts);
		if (ret)
			goto bail;

	} else {

		for (i = 1; i < size; i++) {

			snprintf(ref_path, PATH_MAX, "%s_%ld", dest, i);
			ret = do_unlink(ref_path);
			if (ret)
				goto bail;
		}

		ret = do_unlink(orig_path);
		if (ret)
			goto bail;
	}

	MPI_Barrier_Sync();

	root_printf("  *SubTest %d: Stress test with HUGEFILE reflinked.\n",
		    sub_testno++);
	snprintf(orig_path, PATH_MAX, "%s/multi_original_stress_huge_refile",
		 workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);
	strcpy(tmp_dest, dest);
	strcpy(tmp_orig, orig_path);

	if (!rank) {
		ret = prep_orig_file(orig_path, file_size, 1);
		if (ret)
			goto bail;
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail;
	}

	MPI_Barrier_Sync();

	if (rank) {

		offset = 0;
		interval = file_size / 1000;
		i = 0;
		while (offset < file_size) {

			snprintf(dest, PATH_MAX, "%s_%d_%ld", tmp_dest,
				 rank, i);
			ret = reflink(orig_path, dest, 1);
			if (ret)
				goto bail;

			write_size = get_rand(1, M_SIZE);
			get_rand_buf(write_buf, write_size);

			verify_size = get_rand(M_SIZE, 2 * M_SIZE);

			if (offset < (verify_size - write_size) / 2)
				verify_offset = 0;
			else
				verify_offset = offset -
						(verify_size - write_size) / 2;

			if (verify_offset + verify_size > file_size)
				verify_size = file_size - verify_offset;

			ret = read_at_file(orig_path, pattern_buf, verify_size,
					   verify_offset);
			if (ret)
				goto bail;

			ret = write_at_file(dest, write_buf, write_size,
					    offset);
			if (ret)
				goto bail;

			ret = read_at_file(orig_path, verify_buf, verify_size,
					   verify_offset);
			if (ret)
				goto bail;

			if (memcmp(pattern_buf, verify_buf, verify_size)) {
				abort_printf("Verify original file date failed"
					     " after writting to snapshot!\n");
				ret = -1;
				goto bail;
			}

			offset = offset + write_size + interval;

			strcpy(orig_path, dest);
			i++;
		}

		for (j = 0; j < i; j++) {

			snprintf(dest, PATH_MAX, "%s_%d_%ld", tmp_dest,
				 rank, j);

			ret = do_unlink(dest);
			if (ret)
				goto bail;
		}

		strcpy(dest, tmp_dest);
		offset = 0;
		interval = file_size / 1000;

		while (offset < file_size) {

			write_size = get_rand(1, M_SIZE * 2);
			get_rand_buf(write_buf, write_size);
			ret = write_at_file(dest, write_buf,
					    write_size, offset);
			if (ret)
				goto bail;

			offset = offset + write_size + interval;
		}

	}

	MPI_Barrier_Sync();

	if (!rank) {

		ret = do_unlink(dest);
		if (ret)
			goto bail;

		ret = do_unlink(orig_path);
		if (ret)
			goto bail;
	}

bail:
	if (write_buf)
		free(write_buf);

	if (pattern_buf)
		free(pattern_buf);

	if (verify_buf)
		free(verify_buf);

	should_exit(ret);

	return ret;
}

static int dest_test(void)
{
	int ret;
	char dest[PATH_MAX];
	int sub_testno = 1;

	/*
	* Muti-nodes to reflink the original from a node forever,
	* Then manually or automatically crash the target node
	* where reflinking is performing, we next check the validation
	* of original file and restore its correct state by fsck.
	* For target nodes, the reflinked inode should not exist
	* in orphan directory anyway!
	*/

	root_printf("Test %d: Multi-nodes destructive refcount test.\n",
		    testno++);

	snprintf(orig_path, PATH_MAX, "%s/multi_original_basic_refile",
		 workplace);

	/*
	* Note that we use somewhat large files with very separated extents
	* to let the reflinking process relatively slow. which therefore make
	* it easy for us to interrupt.
	*/
	if (!rank) {
		ret = prep_orig_file(orig_path, file_size, 0);
		should_exit(ret);
	}


	MPI_Barrier_Sync();

	if (rank) {

		while (1) {
			snprintf(dest, PATH_MAX, "%s-%s-%d", orig_path,
				 hostname, rank);
			printf("  *SubTest %d:Reflinking to inode %s on %s.\n",
			       sub_testno++, dest, hostname);
			ret = reflink(orig_path, dest, 1);
			should_exit(ret);
			sleep(5);

			printf("  *SubTest %d:Unlinking inode %s from %s.\n",
			       sub_testno++, dest, hostname);
			ret = do_unlink(dest);
			should_exit(ret);
		}
	}

	MPI_Barrier_Sync();

	if (!rank) {

		ret = verify_orig_file(orig_path);
		should_exit(ret);
		ret = do_unlink(orig_path);
		should_exit(ret);
	}

	return 0;
}

static void run_test(void)
{
	int i;

	for (i = 0; i < iteration; i++) {

		root_printf("[*Round %d Test Running*]\n", i);

		if (test_flags & BASC_TEST)
			basic_test();

		if (test_flags & RAND_TEST)
			basic_test();

		if (test_flags & MMAP_TEST)
			basic_test();

		if (test_flags & ODCT_TEST)
			directio_test();

		if (test_flags & XATR_TEST)
			xattr_test();

		if (test_flags & STRS_TEST)
			stress_test();

		if (test_flags & DEST_TEST)
			dest_test();

		if (test_flags & COMP_TEST)
			comp_test();
	}
}

int main(int argc, char **argv)
{

	setup(argc, argv);

	run_test();

	teardown(MPI_RET_SUCCESS);

	return 0;
}
