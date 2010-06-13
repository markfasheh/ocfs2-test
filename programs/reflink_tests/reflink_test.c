/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reflink_test.c
 *
 * reflink testing binary for single node,it includes functionality,
 * stress,concurrent,random and boundary tests by specifying various
 * arguments.
 *
 * Written by tristan.ye@oracle.com
 *
 * XXX: This could easily be turned into an mpi program.
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

static char device[100];

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

static char fh_log_orig[PATH_MAX];
static char fh_log_dest[PATH_MAX];

static char dest_log_path[PATH_MAX];

static char lsnr_addr[HOSTNAME_LEN];

static int iteration = 1;
static int testno = 1;
static unsigned long port = 9999;

static unsigned long ref_counts = 10;
static unsigned long ref_trees = 10;
static unsigned long child_nums = 10;
static unsigned long hole_nums = 100;

static pid_t *child_pid_list;

int test_flags = 0x00000000;

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

static unsigned long list_sz;

char value_prefix_magic[] = "abcdefghijklmnopqrs";
char value_postfix_magic[] = "srqponmlkjihgfedcba";
char value_prefix_get[20];
char value_postfix_get[20];
char value_sz[6];
char value_sz_get[6];
char *name_get;


static void usage(void)
{
	printf("Usage: reflink_tests [-i iteration] <-n ref_counts> "
	       "<-p refcount_tree_pairs> <-l file_size> <-d disk> "
	       "<-w workplace> -f -b [-c conc_procs] -m -s -r [-x xattr_nums]"
	       " [-h holes_num] [-o holes_filling_log] -O -D <child_nums> -I -H -T\n\n"
	       "-f enable basic feature test.\n"
	       "-b enable boundary test.\n"
	       "-c enable concurrent tests with conc_procs processes.\n"
	       "-m enable mmap test.\n"
	       "-r enable random test.\n"
	       "-s enable stress test.\n"
	       "-O enable O_DIRECT test.\n"
	       "-D enable destructive test.\n"
	       "-v enable verification for destructive test.\n"
	       "-H enable CoW verification test for punching holes.\n"
	       "-T enable CoW verification test for truncating.\n"
	       "-I enable inline-data test.\n"
	       "-x enable combination test with xattr.\n"
	       "-h enable holes punching and filling tests.\n"
	       "-o specify logfile for holes filling tests,it takes effect"
	       " when -h enabled.\n"
	       "-p specify number of refcount trees in fs.\n"
	       "-a specify listener's ip addr for destructive test.\n"
	       "-P specify listener's listening port for destructive test.\n"
	       "iteration specify the running times.\n"
	       "ref_counts specify the reflinks number for one shared inode.\n"
	       "refcount_tree_pairs specify the refcount tree numbers in fs.\n"
	       "file_size specify the file size for reflinks.\n"
	       "disk specify the target volume.\n"
	       "workplace specify the dir where tests will happen.\n\n");
	exit(1);
}

static int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv,
			   "i:d:w:IOfFbBsSrRHTmMW:n:N:"
			   "l:L:c:C:p:x:X:h:o:v:a:P:D:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			iteration = atol(optarg);
			break;
		case 'n':
		case 'N':
			ref_counts = atol(optarg);
			break;
		case 'p':
			ref_trees = atol(optarg);
			break;
		case 'l':
		case 'L':
			file_size = atol(optarg);
			break;
		case 'd':
			strcpy(device, optarg);
			break;
		case 'O':
			test_flags |= ODCT_TEST;
			break;
		case 'D':
			test_flags |= DSCV_TEST;
			child_nums = atol(optarg);
			break;
		case 'I':
			test_flags |= INLN_TEST;
			xattr_nums = 100;
			break;
		case 'w':
		case 'W':
			strcpy(workplace, optarg);
			break;
		case 'o':
			strcpy(fh_log_orig, optarg);
			break;
		case 'v':
			strcpy(dest_log_path, optarg);
			test_flags |= VERI_TEST;
			break;
		case 'a':
			strcpy(lsnr_addr, optarg);
			break;
		case 'f':
		case 'F':
			test_flags |= BASC_TEST;
			break;
		case 'r':
		case 'R':
			test_flags |= RAND_TEST;
			break;
		case 'b':
		case 'B':
			test_flags |= BOND_TEST;
			break;
		case 's':
		case 'S':
			test_flags |= STRS_TEST;
			break;
		case 'm':
		case 'M':
			test_flags |= MMAP_TEST;
			break;
		case 'c':
		case 'C':
			child_nums = atol(optarg);
			test_flags |= CONC_TEST;
			break;
		case 'x':
		case 'X':
			test_flags |= XATR_TEST;
			xattr_nums = atol(optarg);
			break;
		case 'h':
			test_flags |= HOLE_TEST;
			hole_nums = atol(optarg);
		case 'P':
			port = atol(optarg);
		case 'H':
			test_flags |= PUNH_TEST;
		case 'T':
			test_flags |= TRUC_TEST;

		default:
			break;
		}
	}

	if (strcmp(workplace, "") == 0)
		return EINVAL;

	if (strcmp(device, "") == 0)
		return EINVAL;

	if (test_flags & DSCV_TEST)
		if (strcmp(lsnr_addr, "") == 0)
			return EINVAL;

	return 0;
}

static void setup(int argc, char *argv[])
{
	int ret;
	unsigned long i;

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	ret = open_ocfs2_volume(device);
	if (ret < 0) {
		fprintf(stderr, "Open_ocfs2_volume failed!\n");
		exit(ret);
	}

	orig_pattern = (char *)malloc(PATTERN_SIZE);

	child_pid_list = (pid_t *)malloc(sizeof(pid_t) * child_nums);

	memset(orig_pattern, 0, PATTERN_SIZE);

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);

	if ((test_flags & XATR_TEST) || (test_flags & INLN_TEST)) {
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

	return;
}

static void teardown(void)
{
	unsigned long j;

	if (orig_pattern)
		free(orig_pattern);

	if (child_pid_list)
		free(child_pid_list);

	if ((test_flags & XATR_TEST) || (test_flags & INLN_TEST)) {

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
}

static void should_exit(int is_error)
{
	if (is_error < 0) {
		teardown();
		exit(1);
	}
}

static int basic_test()
{
	int i, ret;
	int sub_testno = 1;

	printf("Test %d: Basic reflink test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/original_basic_refile", workplace);

	for (i = 0; i < 2; i++) {

		printf("  *Use Contiguous Extent = %d.\n", i);
		printf("  *SubTest %d: Prepare file.\n", sub_testno++);
		ret = prep_orig_file(orig_path, file_size, i);
		should_exit(ret);

		printf("  *SubTest %d: Do %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
		should_exit(ret);
		printf("  *SubTest %d: Read %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_reads_on_reflinks(orig_path, ref_counts, file_size,
					   clustersize);
		should_exit(ret);

		printf("  *SubTest %d: Do CoW on %ld reflinks.\n", sub_testno++,
		       ref_counts);
		if (test_flags & RAND_TEST) {
			ret = do_cows_on_write(orig_path, ref_counts, file_size,
					       HUNK_SIZE);
			should_exit(ret);
			ret = verify_orig_file(orig_path);
			should_exit(ret);
		} else {
			printf("  *SubTest %d: Do CoW on %d interval.\n",
			       sub_testno++, blocksize);
			ret = do_cows_on_write(orig_path, ref_counts, file_size,
					       blocksize);
			should_exit(ret);
			printf("  *SubTest %d: Verify Original inode.\n",
			       sub_testno++);
			ret = verify_orig_file(orig_path);
			should_exit(ret);
			printf("  *SubTest %d: Do CoW on %ld interval.\n",
			       sub_testno++, clustersize);
			ret = do_cows_on_write(orig_path, ref_counts, file_size,
					       clustersize);
			should_exit(ret);
			printf("  *SubTest %d: Verify Original inode.\n",
			       sub_testno++);
			ret = verify_orig_file(orig_path);
			should_exit(ret);
			printf("  *SubTest %d: Do CoW on %d interval.\n",
			       sub_testno, HUNK_SIZE);
			ret = do_cows_on_write(orig_path, ref_counts, file_size,
					       HUNK_SIZE);
			should_exit(ret);
			printf("  *SubTest %d: Verify Original inode.\n",
			       sub_testno++);
			ret = verify_orig_file(orig_path);
			should_exit(ret);
		}

		printf("  *SubTest %d: Read %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_reads_on_reflinks(orig_path, ref_counts, file_size,
					   clustersize);
		should_exit(ret);
		printf("  *SubTest %d: Unlink %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_unlinks(orig_path, ref_counts);
		should_exit(ret);
		printf("  *SubTest %d: Verify Original inode.\n", sub_testno++);
		ret = verify_orig_file(orig_path);
		should_exit(ret);

		printf("  *SubTest %d: Do %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_reflinks(orig_path, orig_path, ref_counts, 1);
		should_exit(ret);
		printf("  *SubTest %d: Do CoW on truncate with %ld reflinks.\n",
		       sub_testno++, ref_counts);
		ret = do_cows_on_ftruncate(orig_path, ref_counts, file_size);
		should_exit(ret);
		printf("  *SubTest %d: Verify Original inode.\n", sub_testno++);
		ret = verify_orig_file(orig_path);
		should_exit(ret);
		printf("  *SubTest %d: Unlink %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_unlinks(orig_path, ref_counts);
		should_exit(ret);
		printf("  *SubTest %d: Verify Original inode.\n", sub_testno++);
		ret = verify_orig_file(orig_path);
		should_exit(ret);

		printf("  *SubTest %d: Do %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_reflinks(orig_path, orig_path, ref_counts, 2);
		should_exit(ret);
		printf("  *SubTest %d: Do CoW on append with %ld reflinks.\n",
		       sub_testno++, ref_counts);
		ret = do_appends(orig_path, ref_counts);
		should_exit(ret);
		ret = verify_orig_file(orig_path);
		should_exit(ret);
		printf("  *SubTest %d: Unlink %ld reflinks.\n", sub_testno++,
		       ref_counts);
		ret = do_unlinks(orig_path, ref_counts);
		should_exit(ret);
		ret = verify_orig_file(orig_path);
		should_exit(ret);

		printf(" *SubTest %d: Unlink original file.\n", sub_testno++);
		ret = do_unlink(orig_path);
		should_exit(ret);
	}

	return 0;
}

static int boundary_test()
{
	char dest[PATH_MAX];
	char *write_buf = NULL;
	unsigned long offset = 0, write_size = 0;
	unsigned long extent_size = 0, write_pos = 0;

	int sub_testno = 1, ret;

	printf("Test %d: Boundary reflink test.\n", testno++);
	snprintf(orig_path, PATH_MAX, "%s/original_bound_refile", workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);

	write_buf = (char *)malloc(HUNK_SIZE * 2);

	printf("  *SubTest %d: CoW on extent 1 more byte than max_inline_sz.\n",
	       sub_testno++);
	ret = prep_orig_file(orig_path, max_inline_size + 1, 1);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = 0;
	write_size = 1;
	get_rand_buf(write_buf, 1);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	truncate(dest, 0);
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = max_inline_size;
	write_size = 1;
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	truncate(dest, 0);
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = get_rand(0, max_inline_size);
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	truncate(dest, max_inline_size);
	ret = do_unlink(dest);
	if (ret)
		goto bail;
	ret = verify_orig_file(orig_path);
	if (ret)
		goto bail;

	printf("  *SubTest %d: CoW on extent between max_inline_sz and 1M.\n",
	       sub_testno++);
	extent_size = get_rand(max_inline_size + 1, M_SIZE);
	ret = prep_orig_file(orig_path, extent_size, 1);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = get_rand(0, extent_size - 1);
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;
	ret = verify_orig_file(orig_path);
	if (ret)
		goto bail;

	printf("  *SubTest %d: CoW on 1M extent.\n", sub_testno++);
	ret = prep_orig_file(orig_path, M_SIZE, 1);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = get_rand(0, M_SIZE - 1);
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;
	ret = verify_orig_file(orig_path);
	if (ret)
		goto bail;

	printf("  *SubTest %d: CoW on contiguous 1G extent.\n", sub_testno++);
	ret = prep_orig_file(orig_path, G_SIZE, 1);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = 0;
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = G_SIZE - 1;
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = get_rand(0, G_SIZE - 1);
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = 0;
	while (offset < G_SIZE) {
		write_pos = get_rand(offset, offset + M_SIZE - 1);
		ret = write_at_file(dest, write_buf, write_size, write_pos);
		if (ret)
			goto bail;
		offset += M_SIZE;
	}
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	printf("  *SubTest %d: CoW on incontiguous 1G extent.\n", sub_testno++);
	ret = prep_orig_file(orig_path, G_SIZE, 0);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = 0;
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = G_SIZE - 1;
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = get_rand(0, G_SIZE - 1);
	get_rand_buf(write_buf, write_size);
	ret = write_at_file(dest, write_buf, write_size, offset);
	if (ret)
		goto bail;
	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	offset = 0;
	while (offset < G_SIZE) {
		write_pos = get_rand(offset, offset + M_SIZE - 1);
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(dest, write_buf, write_size, write_pos);
		if (ret)
			goto bail;
		offset += M_SIZE;
	}

	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = do_unlink(orig_path);
bail:
	if (write_buf)
		free(write_buf);

	should_exit(ret);

	return 0;
}

static int stress_test()
{
	char dest[PATH_MAX], tmp_dest[PATH_MAX], tmp_orig[PATH_MAX];
	char *write_buf = NULL;
	char *pattern_buf = NULL;
	char *verify_buf = NULL;
	unsigned long offset = 0, write_size = 0;
	unsigned long verify_size = 0, verify_offset = 0;
	unsigned long i, j, interval = 0;
	int ret;

	int sub_testno = 1;

	write_buf = (char *)malloc(HUNK_SIZE);
	pattern_buf = (char *)malloc(HUNK_SIZE * 2);
	verify_buf = (char *)malloc(HUNK_SIZE * 2);

	printf("Test %d: Stress refcount test.\n", testno++);

	printf("  *SubTest %d: Stress test with tremendous refcount trees.\n",
	       sub_testno++);
	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/original_stress_refile_%ld",
			 workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = prep_orig_file(orig_path, max_inline_size + 1, 1);
		if (ret)
			goto bail;
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/original_stress_refile_%ld",
			 workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		offset = get_rand(0, max_inline_size);
		write_size = 1;
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(dest, write_buf, write_size, offset);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {
		snprintf(orig_path, PATH_MAX, "%s/original_stress_refile_%ld",
			 workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = do_unlink(orig_path);
		if (ret)
			goto bail;
		ret = do_unlink(dest);
		if (ret)
			goto bail;
	}

	fflush(stdout);

	printf("  *SubTest %d: Stress test with tremendous shared inodes on "
	       "one refcount tree.\n", sub_testno++);
	snprintf(orig_path, PATH_MAX, "%s/original_stress_refile", workplace);
	ret = prep_orig_file(orig_path, max_inline_size + 1, 1);
	if (ret)
		goto bail;
	ret = do_reflinks(orig_path, orig_path, ref_counts, 1);
	if (ret)
		goto bail;

	for (i = 0; i < ref_counts; i++) {

		if (get_rand(0, 1))
			continue;

		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);

		offset = 0;
		while (offset < max_inline_size + 1) {
			write_size = get_rand(1, max_inline_size);
			get_rand_buf(write_buf, write_size);
			ret = write_at_file(dest, write_buf, write_size,
					    offset);
			if (ret)
				goto bail;
			offset += write_size;
		}

	}

	for (i = 0; i < ref_counts; i++) {

		if (get_rand(0, 1))
			continue;

		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);

		truncate(dest, 0);
	}

	ret = do_unlinks(orig_path, ref_counts);
	if (ret)
		goto bail;
	ret = do_unlink(orig_path);
	if (ret)
		goto bail;

	fflush(stdout);

	printf("  *SubTest %d: Stress test with HUGEFILE reflinked.\n",
	       sub_testno++);
	/*Use a 10G file for testing?*/
	snprintf(orig_path, PATH_MAX, "%s/original_stress_refile", workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);
	ret = prep_orig_file(orig_path, file_size, 1);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;
	strcpy(tmp_dest, dest);
	strcpy(tmp_orig, orig_path);

	/*Then write original and target file randomly*/
	offset = 0;
	interval = file_size / 1000;
	i = 0;
	while (offset < file_size) {

		snprintf(dest, PATH_MAX, "%s_%ld", tmp_dest, i);
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail;

		write_size = get_rand(1, M_SIZE);
		get_rand_buf(write_buf, write_size);

		verify_size = get_rand(M_SIZE, 2 * M_SIZE);

		if (offset < (verify_size - write_size) / 2)
			verify_offset = 0;
		else
			verify_offset = offset - (verify_size - write_size) / 2;

		if (verify_offset + verify_size > file_size)
			verify_size = file_size - verify_offset;

		ret = read_at_file(orig_path, pattern_buf, verify_size,
				   verify_offset);
		if (ret)
			goto bail;

		ret = write_at_file(dest, write_buf, write_size, offset);
		if (ret)
			goto bail;

		sync();

		ret = read_at_file(orig_path, verify_buf, verify_size,
				   verify_offset);
		if (ret)
			goto bail;

		if (memcmp(pattern_buf, verify_buf, verify_size)) {
			fprintf(stderr, "Verify original file date failed "
				"after writting to snapshots!\n");
			should_exit(-1);
		}

		offset = offset + write_size + interval;

		strcpy(orig_path, dest);
		i++;
	}

	for (j = 0; j < i; j++) {

		snprintf(dest, PATH_MAX, "%s_%ld", tmp_dest, j);

		ret = do_unlink(dest);
		if (ret)
			goto bail;
	}

	strcpy(orig_path, tmp_orig);

	ret = do_reflinks_at_random(orig_path, orig_path, ref_counts);
	if (ret)
		goto bail;

	ret = do_unlinks(orig_path, ref_counts);
	if (ret)
		goto bail;

	strcpy(dest, tmp_dest);
	offset = 0;
	interval = file_size / 1000;

	while (offset < file_size) {

		write_size = get_rand(1, M_SIZE);
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(dest, write_buf, write_size, offset);
		if (ret)
			goto bail;

		offset = offset + write_size + interval;
	}

	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = do_unlink(orig_path);
	if (ret)
		goto bail;

	fflush(stdout);

	printf("  *SubTest %d: Stress test with enormous extents in "
	       "1M hunks.\n", sub_testno++);
	/*Use a 10G file for testing?*/
	snprintf(orig_path, PATH_MAX, "%s/original_stress_refile", workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);
	ret = prep_orig_file(orig_path, file_size, 0);
	if (ret)
		goto bail;
	ret = reflink(orig_path, dest, 1);
	if (ret)
		goto bail;

	/*Then write original and target file randomly*/
	offset = 0;
	interval = file_size / 1000;

	while (offset < file_size) {

		write_size = get_rand(1, M_SIZE);
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(orig_path, write_buf, write_size, offset);
		if (ret)
			goto bail;

		offset = offset + write_size + interval;
	}

	ret = do_unlink(dest);
	if (ret)
		goto bail;

	ret = do_unlink(orig_path);
bail:
	if (write_buf)
		free(write_buf);

	if (pattern_buf)
		free(pattern_buf);

	if (verify_buf)
		free(verify_buf);

	should_exit(ret);

	return 0;
}

static void sigchld_handler()
{
	pid_t pid;
	union wait status;

	while (1) {
		pid = wait3(&status, WNOHANG, NULL);
		if (pid <= 0)
			break;
	}
}

static void kill_all_children()
{
	int i;

	for (i = 0; i < child_nums; i++)
		kill(child_pid_list[i], SIGTERM);

	free(child_pid_list);
}

static void sigint_handler()
{
	kill_all_children();

	signal(SIGINT, SIG_DFL);
	kill(getpid(), SIGINT);
}

static void sigterm_handler()
{
	kill_all_children();

	signal(SIGTERM, SIG_DFL);
	kill(getpid(), SIGTERM);
}

static int concurrent_test()
{
	int ret, rc;
	int i, j, status, choice;
	char reflink_path[PATH_MAX];
	pid_t pid;

	printf("Test %d: Concurrent test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/original_conc_refile", workplace);
	ret = prep_orig_file(orig_path, file_size, 0);
	should_exit(ret);
	do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	/*flush out the father's i/o buffer*/
	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	for (i = 0; i < child_nums; i++) {

		pid = fork();

		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			return pid;
		}

		/* child to do CoW*/
		/* and some increment the refcount, some decrement*/
		if (pid == 0) {

			srand(getpid());

			choice = get_rand(0, 3);

			if (choice == 0) {
				/* do reflink to increment refcount*/
				for (j = 0; j < ref_counts; j++) {
					snprintf(reflink_path, PATH_MAX,
						 "%s-%d-%d", orig_path,
						 getpid(), j);
					ret = reflink(orig_path, reflink_path,
						      1);
					should_exit(ret);
				}

				sleep(2);

				/*should unlink newly reflinked inodes lastly*/
				for (j = 0; j < ref_counts; j++) {
					snprintf(reflink_path, PATH_MAX,
						 "%s-%d-%d", orig_path,
						 getpid(), j);
					ret = do_unlink(reflink_path);
					should_exit(ret);
				}
			}

			if (choice == 1) {
				/* do cow to decrement refcount*/
				ret = do_cows_on_write(orig_path, ref_counts,
						       file_size, clustersize);
				should_exit(ret);
			}

			if (choice == 2) {
				/* do truncate and append*/
				if (!(test_flags & MMAP_TEST)) {
					ret = do_cows_on_ftruncate(orig_path,
								   ref_counts,
								   file_size);
					should_exit(ret);
				}

				ret = do_appends(orig_path, ref_counts);
				should_exit(ret);
			}

			if (choice == 3) {
				/* do read */
				ret = verify_orig_file(orig_path);
				should_exit(ret);
			}

			sleep(2);
			exit(0);
		}

		if (pid > 0)
			child_pid_list[i] = pid;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	/*father wait all children to leave*/
	for (i = 0; i < child_nums; i++) {
		ret = waitpid(child_pid_list[i], &status, 0);
		rc = WEXITSTATUS(status);
		if (rc) {
			fprintf(stderr, "Child %d exits abnormally with "
				"RC=%d\n", child_pid_list[i], rc);
		}
	}

	/* father to verify original unchanged file*/
	ret = verify_orig_file(orig_path);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);
	ret = verify_orig_file(orig_path);
	should_exit(ret);

	ret = do_unlink(orig_path);
	should_exit(ret);

	return 0;
}

static int xattr_boundary_test(int ea_nums, int ea_name_size,
			       unsigned long ea_value_size)
{
	int fd, fr, ret;
	unsigned long i;

	xattr_nums = ea_nums;
	xattr_name_sz = ea_name_size;
	xattr_value_sz = ea_value_size;

	fd = open64(orig_path, open_rw_flags);

	/*Prepare xattrs for original inode*/
	for (i = 0; i < xattr_nums; i++) {
		xattr_name_generator(i, USER, xattr_name_sz, xattr_name_sz);
		xattr_value_constructor(i);
		ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
		should_exit(ret);
		ret = read_ea(NORMAL, fd);
		should_exit(ret);
		ret = xattr_value_validator(i);
		should_exit(ret);
	}

	ret = reflink(orig_path, ref_path, 1);
	should_exit(ret);

	fr = open64(ref_path, open_rw_flags);

	for (i = 0; i < xattr_nums; i++) {
		if ((i % 2) == 0)
			xattr_value_sz = XATTR_VALUE_TO_CLUSTER - 2;
		else
			xattr_value_sz = XATTR_VALUE_TO_CLUSTER + 2;

		strcpy(xattr_name, xattr_name_list_set[i]);
		xattr_value_constructor(i);
		ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
		should_exit(ret);
		ret = read_ea(NORMAL, fr);
		should_exit(ret);
		ret = xattr_value_validator(i);
		should_exit(ret);
	}

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	for (i = 0; i < xattr_nums; i++) {
		if (get_rand(0, 1))
			continue;
		strcpy(xattr_name, xattr_name_list_set[i]);
		ret = remove_ea(NORMAL, fr);
		should_exit(ret);
	}

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	for (i = 0; i < xattr_nums; i++) {
		strcpy(xattr_name, xattr_name_list_set[i]);
		ret = remove_ea(NORMAL, fd);
		should_exit(ret);
	}

	ret = do_unlink(ref_path);
	should_exit(ret);

	close(fd);
	close(fr);

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
			should_exit(ret);

			if (xattr_value_sz > xattr_name_sz + 50) {
				ret = read_ea(NORMAL, fd);
				should_exit(ret);
				ret = xattr_value_validator(j);
				should_exit(ret);
			}
		}

		close(fd);
	}

	return 0;
}

static int do_xattr_data_cows(char *ref_pfx, unsigned long iter, int ea_nums)
{
	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret = 0;

	unsigned long offset = 0, write_size = 0;
	char *write_buf = NULL;

	write_buf = (char *)malloc(HUNK_SIZE);

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);

		fd = open64(dest, open_rw_flags);
		if (fd < 0) {
			ret = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, ret, strerror(ret));
			ret = fd;
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
			if (ret)
				goto bail;

			if (xattr_value_sz > xattr_name_sz + 50) {
				ret = read_ea(NORMAL, fd);
				if (ret)
					goto bail;
				ret = xattr_value_validator(j);
				if (ret)
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

	should_exit(ret);

	return ret;
}

static int do_xattr_reads(char *ref_pfx, unsigned long iter, int ea_nums)
{
	unsigned long i, j;
	char dest[PATH_MAX];

	int fd, ret, o_ret;

	for (i = 0; i < iter; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", ref_pfx, i);
		strcpy(filename, dest);

		fd = open64(dest, open_ro_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				dest, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}

		for (j = 0; j < ea_nums; j++) {

			strcpy(xattr_name, xattr_name_list_set[j]);
			memset(xattr_value_get, 0, xattr_value_sz);
			ret = read_ea(NORMAL, fd);
			should_exit(ret);
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
			should_exit(ret);

			if (get_rand(0, 1))
				continue;

			xattr_value_sz = get_rand(1, XATTR_VALUE_MAX_SZ);

			if (xattr_value_sz > xattr_name_sz + 50)
				xattr_value_constructor(j);
			else
				xattr_value_generator(j, xattr_value_sz,
						      xattr_value_sz);

			ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
			should_exit(ret);

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
		should_exit(ret);
	}

	return 0;
}

static int xattr_basic_test(int ea_nums, int ea_name_size,
			    unsigned long ea_value_size)
{
	int fd, ret;
	unsigned long i;
	int sub_testno = 1;

	xattr_nums = ea_nums;
	xattr_name_sz = ea_name_size;
	xattr_value_sz = ea_value_size;

	snprintf(orig_path, PATH_MAX, "%s/original_xattr_basic_refile",
		 workplace);

	printf("  *SubTest %d: Prep original inode with %ld EAs.\n",
	       sub_testno++, xattr_nums);

	ret = prep_orig_file(orig_path, file_size, 1);
	should_exit(ret);
	fd = open64(orig_path, open_rw_flags);

	for (i = 0; i < ea_nums; i++) {

		memset(xattr_name, 0, xattr_name_sz + 1);
		xattr_name_generator(i, USER, xattr_name_sz, xattr_name_sz);
		xattr_value_constructor(i);
		ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
		should_exit(ret);
		ret = read_ea(NORMAL, fd);
		should_exit(ret);
		ret = xattr_value_validator(i);
		should_exit(ret);
	}

	printf("  *SubTest %d: Updating %ld reflinks' %ld EAs to CoW.\n",
	       sub_testno++, ref_counts, xattr_nums);
	/*Updating xattr of reflinks to cause CoW*/
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	ret = do_xattr_cows(orig_path, ref_counts, xattr_nums);
	should_exit(ret);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	printf("  *SubTest %d: Removing %ld reflinks' %ld EAs randomly.\n",
	       sub_testno++, ref_counts, xattr_nums);
	/*Random remove&add ea*/
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	ret = do_xattr_removes(orig_path, ref_counts, xattr_nums);
	should_exit(ret);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	/*Data&Xattr updating combination test*/
	printf("  *SubTest %d: Updating %ld reflinks' EAs&File-data together"
	       " to CoW.\n", sub_testno++, ref_counts);
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	ret = do_xattr_data_cows(orig_path, ref_counts, xattr_nums);
	should_exit(ret);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	/*Xattr read test*/
	printf("  *SubTest %d: Reading %ld reflinks' %ld EAs randomly.\n",
	       sub_testno++, ref_counts, xattr_nums);
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	xattr_value_sz = XATTR_VALUE_MAX_SZ;
	ret = do_xattr_reads(orig_path, ref_counts, xattr_nums);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	/*Xattr list test*/
	printf("  *SubTest %d: Listing %ld reflinks' EAs.\n", sub_testno++,
	       ref_counts);
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	if (list_sz < XATTR_LIST_MAX_SZ) {
		xattr_value_sz = ea_value_size;
		ret = do_xattr_lists(orig_path, ref_counts);
		should_exit(ret);
	}

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);


	printf("  *SubTest %d: Unlinking reflinks and original inode.\n",
	       sub_testno++);
	/*Remove original inode*/
	for (i = 0; i < xattr_nums; i++) {
		strcpy(xattr_name, xattr_name_list_set[i]);
		ret = remove_ea(NORMAL, fd);
		should_exit(ret);
	}

	close(fd);

	ret = do_unlink(orig_path);
	should_exit(ret);

	return 0;
}

static int xattr_random_reflink_test(int ea_nums, int ea_name_size,
				     int ea_value_size, unsigned long ref_nums)
{
	int fd, ret;
	unsigned long i, j;
	unsigned long ea_index = 0, ea_count = 0;

	char dest[PATH_MAX], tmp_dest[PATH_MAX];

	xattr_nums = ea_nums;
	xattr_name_sz = ea_name_size;
	xattr_value_sz = ea_value_size;
	ea_count = xattr_nums;

	snprintf(orig_path, PATH_MAX, "%s/original_xattr_random_refile",
		workplace);
	snprintf(dest, PATH_MAX, "%s_target", orig_path);
	strcpy(tmp_dest, dest);

	ret = prep_orig_file(orig_path, file_size, 1);
	should_exit(ret);
	fd = open64(orig_path, open_rw_flags);

	for (i = 0; i < xattr_nums; i++) {

		memset(xattr_name, 0, xattr_name_sz + 1);
		xattr_name_generator(i, USER, xattr_name_sz, xattr_name_sz);
		xattr_value_constructor(i);
		ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
		should_exit(ret);
	}

	for (j = 0; j < ref_nums; j++) {

		if (j % 3 == 0) {
			memset(xattr_name, 0, xattr_name_sz + 1);
			xattr_name_generator(ea_count, USER, xattr_name_sz,
					     xattr_name_sz);
			xattr_value_constructor(ea_count);
			ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
			should_exit(ret);

			ea_count++;
		}

		if (j % 3 == 1) {

			ea_index = get_rand(0, ea_count);
			while (!xattr_name_list_set[ea_index])
				ea_index = get_rand(0, ea_count);

			strcpy(xattr_name, xattr_name_list_set[ea_index]);
			xattr_value_constructor(ea_index);
			ret = add_or_update_ea(NORMAL, fd, XATTR_REPLACE,
					       "update");
			should_exit(ret);
		}

		if (j % 3 == 2) {

			ea_index = get_rand(0, ea_count);
			while (!xattr_name_list_set[ea_index])
				ea_index = get_rand(0, ea_count);

			strcpy(xattr_name, xattr_name_list_set[ea_index]);
			xattr_name_list_set[ea_index] = NULL;
			ret = remove_ea(NORMAL, fd);
			should_exit(ret);
		}

		snprintf(dest, PATH_MAX, "%s_%ld", tmp_dest, j);
		ret = reflink(orig_path, dest, 1);
		should_exit(ret);

	}


	for (j = 0; j < ref_nums; j++) {

		snprintf(dest, PATH_MAX, "%s_%ld", tmp_dest, j);
		ret = do_unlink(dest);
		should_exit(ret);

	}

	return 0;
}

static int xattr_combin_test()
{
	/* think about tests with ea together
	*  also should enable boundary tests with ea and file data.
	*/

	unsigned long old_xattr_nums, old_ref_counts;

	int sub_testno = 1, ret, fd, fr;

	/*boundary test for xattr refcount*/

	printf("Test %d: Boundary xattr refcount test.\n", testno++);
	snprintf(orig_path, PATH_MAX, "%s/original_xattr_bound_refile",
		 workplace);
	snprintf(ref_path, PATH_MAX, "%s_target", orig_path);
	old_xattr_nums = xattr_nums;
	old_ref_counts = ref_counts;

	printf("  *SubTest %d: Reflink with small value ea.\n", sub_testno++);
	ret = prep_orig_file(orig_path, max_inline_size + 1, 1);
	should_exit(ret);
	fd = open64(orig_path, open_rw_flags);

	xattr_nums = 1;
	xattr_name_sz = XATTR_NAME_LEAST_SZ;
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER - 4;

	xattr_name_generator(0, USER, xattr_name_sz, xattr_name_sz);
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
	should_exit(ret);
	ret = read_ea(NORMAL, fd);
	should_exit(ret);
	ret = xattr_value_validator(0);
	should_exit(ret);
	ret = reflink(orig_path, ref_path, 1);
	should_exit(ret);
	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	fr = open64(ref_path, open_rw_flags);
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER - 4;
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
	should_exit(ret);
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER - 2;
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
	should_exit(ret);
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER + 2;
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
	should_exit(ret);

	ret = read_ea(NORMAL, fr);
	should_exit(ret);
	ret = xattr_value_validator(0);
	should_exit(ret);
	ret = remove_ea(NORMAL, fr);
	should_exit(ret);
	ret = do_unlink(ref_path);
	should_exit(ret);
	close(fr);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);
	ret = remove_ea(NORMAL, fd);
	should_exit(ret);

	printf("  *SubTest %d: Reflink with large value ea.\n", sub_testno++);
	xattr_nums = 1;
	xattr_name_sz = XATTR_NAME_LEAST_SZ;
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER + 2;

	xattr_name_generator(0, USER, xattr_name_sz, xattr_name_sz);
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fd, XATTR_CREATE, "add");
	should_exit(ret);
	ret = read_ea(NORMAL, fd);
	should_exit(ret);
	ret = xattr_value_validator(0);
	should_exit(ret);
	ret = reflink(orig_path, ref_path, 1);
	should_exit(ret);
	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);

	fr = open64(ref_path, open_rw_flags);
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER + 2;
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
	should_exit(ret);
	xattr_value_sz = XATTR_VALUE_TO_CLUSTER - 2;
	xattr_value_constructor(0);
	ret = add_or_update_ea(NORMAL, fr, XATTR_REPLACE, "update");
	should_exit(ret);
	ret = read_ea(NORMAL, fr);
	should_exit(ret);
	ret = xattr_value_validator(0);
	should_exit(ret);

	ret = remove_ea(NORMAL, fr);
	should_exit(ret);
	ret = do_unlink(ref_path);
	should_exit(ret);
	close(fr);

	ret = verify_orig_file_xattr(NORMAL, orig_path, list_sz);
	should_exit(ret);
	ret = remove_ea(NORMAL, fd);
	should_exit(ret);

	printf("  *SubTest %d: Reflink with clustered-extented-block-xattr"
	       "(large value).\n", sub_testno++);
	xattr_boundary_test(20, XATTR_NAME_LEAST_SZ,
			    XATTR_VALUE_TO_CLUSTER + 2);

	printf("  *SubTest %d: Reflink with none-clustered-extented-block-xattr"
	       "(small value).\n", sub_testno++);
	xattr_boundary_test(20, XATTR_NAME_LEAST_SZ,
			    XATTR_VALUE_TO_CLUSTER - 2);

	printf("  *SubTest %d: Reflink with clustered-extented-bucket-xattr"
	       "(large value).\n", sub_testno++);
	xattr_boundary_test(60, XATTR_NAME_LEAST_SZ,
			    XATTR_VALUE_TO_CLUSTER + 2);

	printf("  *SubTest %d: Reflink with none-clustered-extented-bucket-"
	       "xattr (small value).\n", sub_testno++);
	xattr_boundary_test(60, XATTR_NAME_LEAST_SZ,
			    XATTR_VALUE_TO_CLUSTER - 2);

	ret = do_unlink(orig_path);
	should_exit(ret);

	fflush(stdout);

	printf("  *SubTest %d: Basic xattr refcount test without value "
	       "clusters.\n", sub_testno++);
	ret = xattr_basic_test(50, XATTR_NAME_LEAST_SZ,
			       XATTR_VALUE_TO_CLUSTER - 2);
	should_exit(ret);

	ret = xattr_basic_test(50, XATTR_NAME_LEAST_SZ,
			       XATTR_VALUE_TO_CLUSTER + 2);
	should_exit(ret);

	fflush(stdout);

	xattr_nums = old_xattr_nums;

	if (xattr_nums < 10000)
		goto bail;

	/* Stress test with huge xattr entires and huge value size*/
	/* Note that there is no more list ea test however*/

	xattr_nums = old_xattr_nums;

	printf("  *SubTest %d: Stress xattr refcout test with max name size.\n",
	       sub_testno++);
	ret = xattr_basic_test(100, XATTR_NAME_MAX_SZ,
			       XATTR_NAME_MAX_SZ + 60);
	should_exit(ret);

	fflush(stdout);

	xattr_nums = old_xattr_nums;
	ref_counts = 100;
	printf("  *SubTest %d: Stress xattr refcount test with max value.\n",
	       sub_testno++);
	ret = xattr_basic_test(100, XATTR_NAME_LEAST_SZ, XATTR_VALUE_MAX_SZ);
	should_exit(ret);

	fflush(stdout);

	printf("  *SubTest %d: Stress xattr refcout test with all huge args.\n",
	      sub_testno++);
	xattr_nums = old_xattr_nums;
	ret = xattr_basic_test(xattr_nums, XATTR_NAME_MAX_SZ,
			       XATTR_VALUE_MAX_SZ);
	should_exit(ret);

	fflush(stdout);

	printf("  *SubTest %d: Stress xattr random reflink tests.\n",
	       sub_testno++);
	xattr_nums = old_xattr_nums;
	ref_counts = old_ref_counts;
	ret = xattr_random_reflink_test(xattr_nums / 2, XATTR_NAME_MAX_SZ,
					XATTR_VALUE_MAX_SZ, xattr_nums / 4);
	should_exit(ret);

	fflush(stdout);

bail:
	return 0;
}

static int holes_fill_test(void)
{
	unsigned long i, j;
	char dest[PATH_MAX];
	struct write_unit wu;

	int ret;

	FILE *log;

	printf("Test %d: Holes filling&punching test.\n", testno++);

	snprintf(orig_path, PATH_MAX, "%s/original_holes_refile",
		 workplace);
	prep_file_with_hole(orig_path, file_size);
	do_reflinks(orig_path, orig_path, ref_counts, 0);

	for (i = 0; i < ref_counts; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);
		snprintf(fh_log_dest, PATH_MAX, "%sr%ld", fh_log_orig, i);
		log = open_logfile(fh_log_dest);

		for (j = 0; j < hole_nums; j++) {

			prep_rand_write_unit(&wu);

			log_write(log, &wu);

			ret = do_write_file(dest, &wu);

			if (ret)
				return -1;
		}

		fclose(log);
	}

	log = open_logfile(fh_log_orig);

	for (j = 0; j < hole_nums; j++) {

		prep_rand_write_unit(&wu);

		log_write(log, &wu);

		ret = do_write_file(orig_path, &wu);
		if (ret)
			return -1;
	}

	fclose(log);

	return 0;
}

static int destructive_test(void)
{
	int o_flags_rw, o_flags_ro, sockfd, i, j, status;
	int ret, o_ret, fd, rc, sub_testno = 1;
	char log_rec[1024], dest[PATH_MAX];

	struct dest_write_unit dwu;

	unsigned long align_slice = CHUNK_SIZE;
	unsigned long align_filesz = align_slice;
	unsigned long chunk_no = 0;

	pid_t pid;

	int sem_id;
	key_t sem_key = IPC_PRIVATE;

	/*get and init semaphore*/
	sem_id = semget(sem_key, 1, 0766 | IPC_CREAT);
	if (sem_id < 0) {
		sem_id = errno;
		fprintf(stderr, "semget failed, %s.\n", strerror(sem_id));
		return -1;
	}

	ret = set_semvalue(sem_id, 1);
	if (ret < 0) {
		fprintf(stderr, "Set semaphore value failed!\n");
		return ret;
	}

	while (align_filesz < file_size)
		align_filesz += CHUNK_SIZE;

	chunk_no = file_size / CHUNK_SIZE;

	printf("Test %d: Destructive reflink test.\n", testno);

	o_flags_rw = open_rw_flags;
	o_flags_ro = open_ro_flags;

	open_rw_flags |= O_DIRECT;
	open_ro_flags |= O_DIRECT;

	snprintf(orig_path, PATH_MAX, "%s/original_destructive_refile",
		 workplace);

	printf("  *SubTest %d: Prepare original file in %ld chunks.\n",
	       sub_testno++, chunk_no);

	ret = prep_orig_file_in_chunks(orig_path, chunk_no);
	should_exit(ret);

	printf("  *SubTest %d: Do reflinks to reflink the extents.\n",
	       sub_testno++);

	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	sync();

	/*flush out the father's i/o buffer*/
	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	printf("  *SubTest %d: Init socket for msg sending\n", sub_testno++);

	sockfd = init_sock(lsnr_addr, port);

	printf("  *SubTest %d: Fork %lu children to write in chunks.\n",
	       sub_testno++, child_nums);

	fd  = open64(orig_path, open_rw_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", orig_path, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	for (i = 0; i < child_nums; i++) {

		pid = fork();

		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			return pid;
		}

		/* child to do CoW*/
		if (pid == 0) {

			srand(getpid());

			for (j = 0; j < chunk_no; j++) {

				if (semaphore_p(sem_id) < 0) {
					ret = -1;
					goto child_bail;
				}

				memset(log_rec, 0, sizeof(log_rec));
				prep_rand_dest_write_unit(&dwu, get_rand(0,
							  chunk_no - 1));
				snprintf(log_rec, sizeof(log_rec), "%lu\t%llu"
					 "\t%d\t%c\n", dwu.d_chunk_no,
					 dwu.d_timestamp, dwu.d_checksum,
					 dwu.d_char);

				ret = do_write_chunk(fd, &dwu);
				if (ret)
					goto child_bail;
				write(sockfd, log_rec, strlen(log_rec) + 1);

				if (semaphore_v(sem_id) < 0) {
					ret = -1;
					goto child_bail;
				}

				if (get_rand(0, 1)) {
					
					if (semaphore_p(sem_id) < 0) {
						ret = -1;
						goto child_bail;
					}

					snprintf(dest, PATH_MAX,
						 "%s_target_%d_%d",
						 orig_path, getpid(), j);
					memset(log_rec, 0, sizeof(log_rec));
					snprintf(log_rec, sizeof(log_rec),
						 "Reflink:\t%s\t->\t%s\n",
						 orig_path, dest);
					ret = reflink(orig_path, dest, 1);
					if (ret)
						goto child_bail;
					write(sockfd, log_rec,
					      strlen(log_rec) + 1);
					
					if (semaphore_v(sem_id) < 0) {
						ret = -1;
						goto child_bail;
					}
				}

				/*
				 * Are you ready to crash the machine?
				*/

				if ((j > 1) && (j < chunk_no - 1)) {
					if (get_rand(1, chunk_no) == chunk_no / 2) {

						if (semaphore_p(sem_id) < 0) {
							ret = -1;
							goto child_bail;
						}

						system("echo b>/proc/sysrq-trigger");
					}
				} else if (j == chunk_no - 1) {

						if (semaphore_p(sem_id) < 0) {
							ret = -1;
							goto child_bail;
						}

						system("echo b>/proc/sysrq-trigger");
				}

				usleep(10000);
			}
child_bail:
			if (fd)
				close(fd);

			if (sockfd)
				close(sockfd);

			if (sem_id)
				semaphore_close(sem_id);

			exit(ret);
		}

		if (pid > 0)
			child_pid_list[i] = pid;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	/*father wait all children to leave*/
	for (i = 0; i < child_nums; i++) {
		ret = waitpid(child_pid_list[i], &status, 0);
		rc = WEXITSTATUS(status);
		if (rc) {
			fprintf(stderr, "Child %d exits abnormally with "
				"RC=%d\n", child_pid_list[i], rc);
		}
	}

	open_rw_flags = o_flags_rw;
	open_ro_flags = o_flags_ro;

	/*
	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	ret = do_unlink(orig_path);
	should_exit(ret);
	*/

	if (fd)
		close(fd);

	if (sockfd)
		close(sockfd);

	if (sem_id)
		semaphore_close(sem_id);

	return 0;
}

static int verification_dest(void)
{

	unsigned long align_slice = CHUNK_SIZE;
	unsigned long align_filesz = align_slice;
	unsigned long chunk_no = 0;
	int ret;

	while (align_filesz < file_size)
		align_filesz += CHUNK_SIZE;

	chunk_no = file_size / CHUNK_SIZE;

	printf("Test %d: Verification for destructive test.\n", testno);

	snprintf(orig_path, PATH_MAX, "%s/original_destructive_refile",
		 workplace);

	ret = verify_dest_files(dest_log_path, orig_path, chunk_no);
	should_exit(ret);

	return ret;
}

static int directio_test(void)
{

	int ret, o_flags_rw, o_flags_ro;
	int sub_testno = 1;

	unsigned long align_slice = 512;
	unsigned long align_filesz = align_slice;

	while (align_filesz < file_size)
		align_filesz += align_slice;


	printf("Test %d: O_DIRECT reflink test.\n", testno);

	o_flags_rw = open_rw_flags;
	o_flags_ro = open_ro_flags;

	open_rw_flags |= O_DIRECT;
	open_ro_flags |= O_DIRECT;

	snprintf(orig_path, PATH_MAX, "%s/original_directio_refile", workplace);

	printf("  *SubTest %d: Prepare original file with O_DIRECT.\n",
	       sub_testno++);

	ret = prep_orig_file_dio(orig_path, align_filesz);
	should_exit(ret);

	printf("  *SubTest %d: Do %ld reflinks.\n",
	       sub_testno++, ref_counts);

	do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);

	printf("  *SubTest %d: Do O_DIRECT reads among reflinks.\n",
	       sub_testno++);
	ret = do_reads_on_reflinks(orig_path, ref_counts, align_filesz,
				   DIRECTIO_SLICE);
	should_exit(ret);

	printf("  *SubTest %d: Do O_DIRECT writes among reflinks.\n",
	       sub_testno++);

	ret = do_cows_on_write(orig_path, ref_counts, align_filesz,
			       DIRECTIO_SLICE);
	should_exit(ret);

	printf("  *SubTest %d: Do O_DIRECT appends among reflinks.\n",
	       sub_testno++);
	ret = do_appends(orig_path, ref_counts);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);


	printf("  *SubTest %d: Do O_DIRECT truncates among reflinks.\n",
	       sub_testno++);
	do_reflinks(orig_path, orig_path, ref_counts, 0);
	should_exit(ret);
	ret = do_cows_on_ftruncate(orig_path, ref_counts, align_filesz);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	printf("  *SubTest %d: Do O_DIRECT random reflinks.\n",
	       sub_testno++);
	ret = do_reflinks_at_random(orig_path, orig_path, ref_counts);
	should_exit(ret);

	ret = do_unlinks(orig_path, ref_counts);
	should_exit(ret);

	ret = do_unlink(orig_path);
	should_exit(ret);

	open_rw_flags = o_flags_rw;
	open_ro_flags = o_flags_ro;

	return 0;
}

static int inline_test(void)
{
	char dest[PATH_MAX];
	char *write_buf = NULL;
	unsigned long offset = 0, write_size = 0;
	unsigned long i = 0;

	int ret, sub_testno = 1;

	printf("Test %d: Reflink on inlined files test.\n", testno++);

	write_buf = (char *)malloc(HUNK_SIZE);

	if (file_size > max_inline_size)
		file_size = get_rand(0, max_inline_size);

	fflush(stdout);

	basic_test();

	fflush(stdout);

	ret = xattr_basic_test(xattr_nums, XATTR_NAME_LEAST_SZ,
			       XATTR_VALUE_TO_CLUSTER + 100);
	if (ret)
		goto bail;

	fflush(stdout);

	printf("  *SubTest %d: Stress test with tremendous refcount trees.\n",
	       sub_testno++);
	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/original_inline_stress_"
			 "refile_%ld", workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = prep_orig_file(orig_path, file_size, 1);
		if (ret)
			goto bail;
		ret = reflink(orig_path, dest, 1);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {

		snprintf(orig_path, PATH_MAX, "%s/original_inline_stress_"
			 "refile_%ld", workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		offset = get_rand(0, max_inline_size);
		write_size = 1;
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(orig_path, write_buf, write_size, offset);
		if (ret)
			goto bail;
		offset = get_rand(max_inline_size, max_inline_size * 2);
		write_size = 1;
		get_rand_buf(write_buf, write_size);
		ret = write_at_file(dest, write_buf, write_size, offset);
		if (ret)
			goto bail;
	}

	for (i = 0; i < ref_trees; i++) {
		snprintf(orig_path, PATH_MAX, "%s/original_inline_stress_"
			 "refile_%ld", workplace, i);
		snprintf(dest, PATH_MAX, "%s_target", orig_path);
		ret = do_unlink(orig_path);
		if (ret)
			goto bail;

		ret = do_unlink(dest);
		if (ret)
			goto bail;
	}

	fflush(stdout);

	printf("  *SubTest %d: Stress test with tremendous shared inodes on "
	       "one refcount tree.\n", sub_testno++);
	snprintf(orig_path, PATH_MAX, "%s/original_inline_stress_refile",
		 workplace);
	ret = prep_orig_file(orig_path, file_size, 1);
	if (ret)
		goto bail;

	ret = do_reflinks(orig_path, orig_path, ref_counts, 1);
	if (ret)
		goto bail;

	for (i = 0; i < ref_counts; i++) {

		if (get_rand(0, 1))
			continue;

		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);

		offset = 0;
		while (offset < file_size) {
			write_size = get_rand(1, file_size);
			get_rand_buf(write_buf, write_size);
			ret = write_at_file(dest, write_buf, write_size,
					    offset);
			if (ret)
				goto bail;
			offset += write_size;
		}

	}

	for (i = 0; i < ref_counts; i++) {

		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);

		if (get_rand(0, 1))
			truncate(dest, 0);
		else
			truncate(dest, max_inline_size + 1);
	}

	ret = do_unlinks(orig_path, ref_counts);
	if (ret)
		goto bail;
	ret = do_unlink(orig_path);
bail:
	if (write_buf)
		free(write_buf);

	should_exit(ret);

	return 0;
}

static int verify_punch_hole_cow_test(void)
{
	int ret = 0, fd;
	int sub_testno = 1;
	char *write_pattern = NULL;
	char *read_pattern = NULL;

	unsigned long i;
	unsigned long long offset, len, read_size;
	char dest[PATH_MAX];

	printf("Test %d: Verify cow for punching holes.\n", testno++);

        snprintf(orig_path, PATH_MAX, "%s/original_verify_cow_punch_hole_"
		 "refile", workplace);

	write_pattern = malloc(clustersize);
	read_pattern = malloc(clustersize);

	get_rand_buf(write_pattern, clustersize);
	memset(read_pattern, 0, clustersize);

	printf("  *SubTest %d: Prepare file.\n", sub_testno++);
	ret = prep_orig_file_with_pattern(orig_path, file_size, clustersize,
					  write_pattern, 0);
	if (ret < 0)
		goto bail;
	printf("  *SubTest %d: Do %ld reflinks.\n", sub_testno++, ref_counts);
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	if (ret < 0)
		goto bail;

	printf("  *SubTest %d: Punching hole to original file.\n",
	       sub_testno++);

	fd = open_file(orig_path, O_RDWR);
	if (fd < 0)
		goto bail;

	offset = 0;

	while (offset < file_size) {

		offset += get_rand(0, clustersize);
		len = get_rand(clustersize, 2 * clustersize);
		if ((offset + len) > file_size)
			len = file_size - offset;

		ret = punch_hole(fd, offset, len);
		if (ret < 0) {
			fprintf(stderr, "failed to punch hole from %llu to "
				"%llu on %s\n", offset,
				offset + len, orig_path);
			close(fd);
			goto bail;
		}	

		offset += len;
	}
	
	close(fd);

	printf("  *SubTest %d: Verify reflinks after punching holes.\n",
	       sub_testno++);

	for (i = 0; i < ref_counts; i++) {
		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);
		fd = open_file(dest, O_RDONLY);
		if (fd < 0)
			goto bail;

		offset = 0;
		while (offset < file_size) {
			if ((offset + clustersize) > file_size)
				read_size = file_size - offset;
			else
				read_size = clustersize;
                        ret = read_at(fd, read_pattern, read_size, offset);
			if (ret < 0) {
				close(fd);
				goto bail;
			}
			if (memcmp(read_pattern, write_pattern, read_size)) {
				fprintf(stderr, "Corrupted chunk found on "
					"reflink %s: from %llu to %llu\n",
					dest, offset, offset + read_size);
				ret = -1;
				close(fd);
				goto bail;
			}
                        offset += read_size;
                }

		close(fd);

	}

	printf("  *SubTest %d: Unlinking reflinks and original file.\n",
	       sub_testno++);

	ret = do_unlinks(orig_path, ref_counts);
	if (ret < 0)
		goto bail;

	ret = do_unlink(orig_path);

bail:
	if (write_pattern)
		free(write_pattern);

	if (read_pattern)
		free(read_pattern);

	return ret;
}

static int verify_truncate_cow_test(void)
{
	int ret = 0, fd;
	int sub_testno = 1;
	char *write_pattern = NULL;
	char *read_pattern = NULL;

	unsigned long i;
	unsigned long long offset, read_size, new_size, trunc_size;
	char dest[PATH_MAX];

	printf("Test %d: Verify cow for truncating.\n", testno++);

        snprintf(orig_path, PATH_MAX, "%s/original_verify_cow_truncate_"
		 "refile", workplace);

	write_pattern = malloc(clustersize);
	read_pattern = malloc(clustersize);

	get_rand_buf(write_pattern, clustersize);
	memset(read_pattern, 0, clustersize);

	printf("  *SubTest %d: Prepare file.\n", sub_testno++);
	ret = prep_orig_file_with_pattern(orig_path, file_size, clustersize,
					  write_pattern, 0);
	if (ret < 0)
		goto bail;

	printf("  *SubTest %d: Do %ld reflinks.\n", sub_testno++, ref_counts);
	ret = do_reflinks(orig_path, orig_path, ref_counts, 0);
	if (ret < 0)
		goto bail;

	printf("  *SubTest %d: Truncating original file.\n",
	       sub_testno++);

	fd = open_file(orig_path, O_RDWR);
	if (fd < 0)
		goto bail;

	new_size = file_size;

	while (new_size >= 0) {

		ret = ftruncate(fd, new_size);
		if (ret < 0) {
			fprintf(stderr, "failed to truncate file %s to %llu\n",
				orig_path, new_size); 
			close(fd);
			goto bail;
		}	

		if (new_size == 0)
			break;

		trunc_size = get_rand(0, new_size);

		new_size -= trunc_size;

	}
	
	close(fd);

	printf("  *SubTest %d: Verify reflinks after punching holes.\n",
	       sub_testno++);

	for (i = 0; i < ref_counts; i++) {
		snprintf(dest, PATH_MAX, "%sr%ld", orig_path, i);
		fd = open_file(dest, O_RDONLY);
		if (fd < 0)
			goto bail;

		offset = 0;
		while (offset < file_size) {
			if ((offset + clustersize) > file_size)
				read_size = file_size - offset;
			else
				read_size = clustersize;
                        ret = read_at(fd, read_pattern, read_size, offset);
			if (ret < 0) {
				close(fd);
				goto bail;
			}
			if (memcmp(read_pattern, write_pattern, read_size)) {
				fprintf(stderr, "Corrupted chunk found on "
					"reflink %s: from %llu to %llu\n",
					dest, offset, offset + read_size);
				ret = -1;
				close(fd);
				goto bail;
			}
                        offset += read_size;
                }

		close(fd);

	}

	printf("  *SubTest %d: Unlinking reflinks and original file.\n",
	       sub_testno++);

	ret = do_unlinks(orig_path, ref_counts);
	if (ret < 0)
		goto bail;

	ret = do_unlink(orig_path);

bail:
	if (write_pattern)
		free(write_pattern);

	if (read_pattern)
		free(read_pattern);

	return ret;
}

static void run_test(void)
{
	int i;

	for (i = 0; i < iteration; i++) {

		printf("Round %d test running...\n", i);

		if (test_flags & BASC_TEST)
			basic_test();

		if (test_flags & RAND_TEST)
			basic_test();

		if ((test_flags & MMAP_TEST) && !(test_flags & HOLE_TEST))
			basic_test();

		if (test_flags & CONC_TEST)
			concurrent_test();

		if (test_flags & BOND_TEST)
			boundary_test();

		if (test_flags & STRS_TEST)
			stress_test();

		if (test_flags & XATR_TEST)
			xattr_combin_test();

		if (test_flags & HOLE_TEST)
			holes_fill_test();

		if (test_flags & ODCT_TEST)
			directio_test();

		if (test_flags & INLN_TEST)
			inline_test();

		if (test_flags & DSCV_TEST)
			destructive_test();

		if (test_flags & VERI_TEST)
			verification_dest();

		if (test_flags & PUNH_TEST)
			verify_punch_hole_cow_test();

		if (test_flags & TRUC_TEST)
			verify_truncate_cow_test();

	}
}

int main(int argc, char *argv[])
{

	setup(argc, argv);

	run_test();

	teardown();

	return 0;
}
