/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * single-inline-data.c
 *
 * Verify I/O to/from files small enough to hold inline data. Includes
 * some tests intended to force an inode out to an extent list.
 *
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
 * [III] Tests for concurrent r/w among multiple processes.
 *    1) propagate multiple process to perform concurrent rw on one inlined file
 *    2) Create multiple files among multi-nodes,and perform r/w separately.
 *
 * XXX: This could easily be turned into an mpi program, where a
 * second node does the verification step.
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

#include <sys/wait.h>
#include <signal.h>

#define PATTERN_SZ			8192
#define OCFS2_MAX_FILENAME_LEN          255

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define WORK_PLACE	"inline-data-test"

static char *prog;
static char device[100];

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
static int do_multi_process_test;
static int do_multi_file_test;
static unsigned long child_nums = 2;
static unsigned long file_nums = 2;

pid_t *child_pid_list_mp = NULL;
pid_t *child_pid_list_mf = NULL;

extern unsigned long get_rand(unsigned long min, unsigned long max);
extern char rand_char(void);
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
extern int open_ocfs2_volume(char *device_name);
extern int is_file_inlined(char *dirent_name, unsigned long *i_size,
			   unsigned int *id_count);
extern int should_inlined_or_not(int is_inlined, int should_inlined,
				 int test_no);

static void usage(void)
{
	printf("Usage: inline-data [-i <iteration>] "
	       "[-c <concurrent_process_num>] [-m <multi_file_num>] "
	       "<-d <device>> <mount_point>\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "files/dirs with inline data.\n\n"
	       "iteration specify the running times.\n"
	       "concurrent_process_num specify the number of concurrent "
	       "multi_file_num specify the number of multiple files"
	       "processes to perform inline-data write/read.\n"
	       "device and mount_point are mandatory.\n");

	exit(1);
}

static int parse_opts(int argc, char **argv)
{
	int c;
	while (1) {
		c = getopt(argc, argv, "D:d:I:i:C:c:M:m:");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
		case 'I':
			iteration = atol(optarg);
			break;
		case 'd':
		case 'D':
			strcpy(device, optarg);
			break;
		case 'c':
		case 'C':
			do_multi_process_test = 1;
			child_nums = atol(optarg);
			break;
		case 'm':
		case 'M':
			do_multi_file_test = 1;
			file_nums = atol(optarg);
			break;
		default:
			break;
		}
	}

	if (strcmp(device, "") == 0)
		return EINVAL;

	if (argc - optind != 1)
		return EINVAL;

	strcpy(mount_point, argv[optind]);
	if (mount_point[strlen(mount_point) - 1] == '/')
		 mount_point[strlen(mount_point) - 1] = '\0';

	return 0;

}

static int setup(int argc, char *argv[])
{
	int ret;

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
		return ret;
	}

	if (do_multi_process_test)
		child_pid_list_mp = (pid_t *)malloc(sizeof(pid_t) * child_nums);
	if (do_multi_file_test)
		child_pid_list_mf = (pid_t *)malloc(sizeof(pid_t) * file_nums);

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

	snprintf(work_place, OCFS2_MAX_FILENAME_LEN, "%s/%s",
		 mount_point, WORK_PLACE);
	mkdir(work_place, FILE_MODE);

	printf("BlockSize:\t\t%u\nMax Inline Data Size:\t%d\n"
	       "ClusterSize:\t\t%lu\nPageSize:\t\t%lu\nWorkingPlace:\t\t%s\n\n",
	       blocksize, max_inline_size, clustersize, page_size, work_place);

	return 0;
}

static int teardown(void)
{
	if (child_pid_list_mp)
		free(child_pid_list_mp);
	if (child_pid_list_mf)
		free(child_pid_list_mf);

	rmdir(work_place);

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

	if (do_multi_process_test) {
		for (i = 0; i < child_nums; i++)
			kill(child_pid_list_mp[i], SIGTERM);
		free(child_pid_list_mp);
	}

	if (do_multi_process_test) {
		for (i = 0; i < file_nums; i++)
			kill(child_pid_list_mf[i], SIGTERM);
		free(child_pid_list_mf);
	}
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

static int concurrent_rw_test(void)
{
	int fd;
	int i, j, status;

	pid_t pid;
	int ret, rc;

	int rand_offset;
	int rand_count;

	fflush(stderr);
	fflush(stdout);

	fd = prep_file(max_inline_size);
	if (fd < 0)
		return -1;

	signal(SIGCHLD, sigchld_handler);

	for (i = 0; i < child_nums; i++) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			return -1;
		}
		/*Children perform random write/read*/
		if (pid == 0) {
			for (j = 0; j < child_nums; j++) {
				rand_offset = get_rand(0, max_inline_size - 1);
				rand_count = get_rand(1, max_inline_size -
						      rand_offset);
				ret = write_at(fd, pattern + rand_offset,
					       rand_count, rand_offset);
				if (ret) {
					fprintf(stderr, "Child %d random "
						"write error!\n", getpid());
					exit(1);
				}

				ret = verify_pattern_fd(fd, max_inline_size);
				if (ret) {
					fprintf(stderr, "Child %d pattern "
						"verification failed!\n",
						getpid());
					exit(1);
				}
			}
			sleep(1);
			exit(0);
		}
		/*Father attempt to control the children*/
		if (pid > 0)
			child_pid_list_mp[i] = pid;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	for (j = 0; j < child_nums; j++) {
		ret =  verify_pattern_fd(fd, max_inline_size);
		if (ret) {
			fprintf(stderr, "Father %d pattern verification"
				" failed!\n", getpid());
			return -1;
		}
	}

	/*father wait all children to leave*/
	for (i = 0; i < child_nums; i++) {
		ret = waitpid(child_pid_list_mp[i], &status, 0);
		rc = WEXITSTATUS(status);
		if (rc) {
			fprintf(stderr, "Child %d exits abnormally with "
				"RC=%d\n", child_pid_list_mp[i], rc);
			return rc;
		}
	}

	close(fd);
	return 0;
}

static int multi_file_rw_test(int test_num)
{
	int fd;
	int i, j, status;

	pid_t pid;
	int ret, rc;

	int rand_offset;
	int rand_count;

	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	for (j = 0; j < file_nums; j++) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Fork process failed!\n");
			return pid;
		}
		if (pid == 0) {
			snprintf(dirent, OCFS2_MAX_FILENAME_LEN,
				"inline-data-test-multi-file-%d", getpid());
			snprintf(file_name, OCFS2_MAX_FILENAME_LEN, "%s/%s",
				 work_place, dirent);
			fd = prep_file(max_inline_size);
			if (fd < 0)
				exit(1);

			for (i = 0; i < file_nums; i++) {
				rand_offset = get_rand(0, max_inline_size - 1);
				rand_count = get_rand(1, max_inline_size -
						      rand_offset);
				ret = write_at(fd, pattern + rand_offset,
					       rand_count, rand_offset);
				if (ret) {
					fprintf(stderr, "Child %d random "
						"write error on %s !\n",
						getpid(), file_name);
					exit(1);
				}
				ret = verify_pattern_fd(fd, max_inline_size);
				if (ret) {
					fprintf(stderr, "Child %d pattern "
						"verification failed on %s!\n",
						getpid(), file_name);
					exit(1);
				}
				fsync(fd);
				sleep(1);
				ret = is_file_inlined(dirent, &i_size,
						      &id_count);
				if (ret < 0)
					return ret;
				else {
					ret = should_inlined_or_not(ret, 1,
								    test_num);
					if (ret < 0)
						return ret;
				}

			}
			close(fd);
			exit(0);
		}
		if (pid > 0)
			child_pid_list_mf[j] = pid;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	/*father wait all children to leave*/
	for (i = 0; i < file_nums; i++) {
		ret = waitpid(child_pid_list_mf[i], &status, 0);
		rc = WEXITSTATUS(status);
		if (rc) {
			fprintf(stderr, "Child %d exists abnormally with "
				"RC=%d\n", child_pid_list_mf[i], rc);
			return rc;
		}
	}

	return 0;
}

static int test_regular_file(int test_no)
{
	int ret, fd, new_size, old_size, init_count, count, j, offset;
	unsigned int test_num = 1;
	int hole_gap = 2;

	snprintf(dirent, OCFS2_MAX_FILENAME_LEN, "inline-data-test-file-%d",
		 getpid());
	snprintf(file_name, OCFS2_MAX_FILENAME_LEN, "%s/%s", work_place,
		 dirent);

	printf("################Test Round %d :%s################\n",
	       test_no, file_name);

	printf("Test  %d: Write basic pattern\n", test_num);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;

	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}

	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;
	test_num++;

	printf("Test  %d: Rewrite portion of basic pattern\n", test_num);
	for (j = 0; j < max_inline_size; j++) {
		count = get_rand(1, max_inline_size - j);
		ret = write_at(fd, pattern + j, count, j);
		if (ret)
			return 1;
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			return 1;
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Reduce size with truncate\n", test_num);
	new_size = max_inline_size;
	for (j = 0; j < max_inline_size; j++) {
		old_size = new_size;
		new_size = max_inline_size - j;
		ret = truncate_pattern(fd, old_size, new_size);
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			return ret;

	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Extend file\n", test_num);
	for (j = 2; j <= max_inline_size; j++) {
		old_size = new_size;
		new_size = j;
		ret = extend_pattern(fd, old_size, new_size);
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			return ret;
	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Extreme truncate and extend\n", test_num);
	old_size = new_size;
	new_size = 0;
	ret = truncate_pattern(fd, old_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return ret;
	old_size = new_size;
	new_size = max_inline_size;
	ret = extend_pattern(fd, old_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return ret;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Double write, both extending\n", test_num);
	for (j = 1; j <= max_inline_size; j++) {
		close(fd);
		fill_pattern(j);
		init_count = get_rand(1, j);
		fd = prep_file_no_fill(init_count, 0);
		if (fd < 0)
			return 1;
		offset = get_rand(1, init_count);
		ret = write_at(fd, pattern + offset, j - offset, offset);
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, j);
		if (ret)
			return ret;
	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Fsync\n", test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	ret = fsync(fd);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Fdatasync\n", test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	ret = fdatasync(fd);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Mmap reads\n", test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	for (j = 1; j <= max_inline_size; j++) {
		ret = verify_pattern_mmap(fd, j);
		if (ret)
			return ret;
	}

	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Reserve space\n", test_num);
	for (j = 0; j < max_inline_size; j++) {
		offset = j;
		count = get_rand(1, max_inline_size - j);
		ret = try_reserve(fd, offset, count);
		if (ret != ENOTTY) {
			if (ret)
				return ret;
			ret = verify_pattern_fd(fd, max_inline_size);
			if (ret)
				return ret;
		}
	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Punch hole\n", test_num);
	offset = 0;
	hole_gap = 10;
	while (offset < max_inline_size)  {
		count = get_rand(1, max_inline_size - offset);
		ret = try_punch_hole(fd, offset, count);
		if (ret != ENOTTY) {
			if (ret)
				return ret;
			ret = verify_pattern_fd(fd, max_inline_size);
			if (ret)
				return ret;
		}
		offset = offset + count + hole_gap;
	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Force expansion to extents via large write\n",
	       test_num);
	close(fd);
	fill_pattern(PATTERN_SZ);
	fd = prep_file_no_fill(max_inline_size, 0);
	if (fd < 0)
		return 1;
	count = PATTERN_SZ - max_inline_size;
	ret = write_at(fd, &pattern[max_inline_size], count, max_inline_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, PATTERN_SZ);
	if (ret)
		return ret;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 0, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Force expansion to extents via mmap write\n",
	       test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	fill_pattern(max_inline_size);
	ret = mmap_write_at(fd, pattern, max_inline_size, 0);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret , 0, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Force expansion to extents via large extend\n",
	       test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	old_size = max_inline_size;
	new_size = 2 * max_inline_size;
	ret = extend_pattern(fd, old_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return 1;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 0, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: Force expansion to extents via large reservation\n",
	       test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	new_size = PATTERN_SZ;
	ret = try_reserve(fd, 0, new_size);
	if (ret != ENOTTY) {
		if (ret)
			return ret;
		ret = extend_pattern(fd, max_inline_size, new_size);
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			return ret;
	}
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 0, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: O_DIRECT read\n", test_num);
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	close(fd);
	fd = open(file_name, O_RDWR|O_DIRECT);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "open (direct) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}
	ret = __verify_pattern_fd(fd, max_inline_size, 1);
	if (ret)
		return 1;
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 1, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	printf("Test  %d: O_DIRECT write\n", test_num);
	close(fd);
	fill_pattern(max_inline_size);
	fd = prep_file_no_fill(max_inline_size, 1);
	if (fd < 0)
		return 1;
	ret = __verify_pattern_fd(fd, max_inline_size, 1);
	if (ret)
		return 1;
	close(fd);
	ret = is_file_inlined(dirent, &i_size, &id_count);
	if (ret < 0)
		return ret;
	else {
		ret = should_inlined_or_not(ret, 0, test_num);
		if (ret < 0)
			return ret;
	}
	test_num++;

	if (do_multi_process_test) {
		printf("Test  %d: Concurrent Write/Read\n", test_num);
		close(fd);
		ret = concurrent_rw_test();
		if (ret < 0)
			return ret;

		ret = is_file_inlined(dirent, &i_size, &id_count);
		if (ret < 0)
			return ret;
		else {
			ret = should_inlined_or_not(ret, 1, test_num);
			if (ret < 0)
				return ret;
		}
		test_num++;
	}

	if (do_multi_file_test) {
		printf("Test  %d: Multiple Files Write/Read\n", test_num);
		ret = multi_file_rw_test(test_num);
		if (ret < 0)
			return ret;
		test_num++;
	}

	printf("All File I/O Tests Passed\n");
	unlink(file_name);

	return ret;
}
int main(int argc, char **argv)
{
	int ret;
	int i;

	ret = setup(argc, argv);
	if (ret)
		return ret;

	for (i = 0; i < iteration; i++) {
		ret = test_regular_file(i);
		if (ret)
			return ret;
	}
	ret = teardown();
	if (ret)
		return ret;

	return 0;
}
