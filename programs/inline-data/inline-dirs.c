/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * single-inline-dirs.c
 *
 * Verify inline directory data.
 *
 * All tests read back the entire directory to verify correctness.
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
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <ocfs2/ocfs2.h>

#include <dirent.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <linux/types.h>


#define OCFS2_MAX_FILENAME_LEN		255
#define WORK_PLACE      		"inline-data-test"
#define MAX_DIRENTS			1024

#define FILE_MODE		(S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

struct my_dirent {
	unsigned int	type;
	unsigned int	name_len;
	unsigned int	seen;
	char		name[OCFS2_MAX_FILENAME_LEN];
};

union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};

static char *prog;
static char device[100];

ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;

static unsigned long page_size;
unsigned int blocksize = 4096;
unsigned long clustersize;
unsigned int max_inline_size;

unsigned int id_count;
unsigned long i_size;

char mount_point[OCFS2_MAX_FILENAME_LEN];
char work_place[OCFS2_MAX_FILENAME_LEN];
char dirent_name[OCFS2_MAX_FILENAME_LEN];
char dir_name[OCFS2_MAX_FILENAME_LEN];

static int iteration = 1;
static int do_multi_process_test;
static int do_multi_file_test;
static unsigned long child_nums = 2;
static unsigned long file_nums = 2;
unsigned int operated_entries = 20;

pid_t *child_pid_list;

int shm_id;
int sem_id;

struct my_dirent *dirents;
unsigned int num_dirents;

char path[PATH_MAX];
char path1[PATH_MAX];

static int testno = 1;

extern unsigned long get_rand(unsigned long min, unsigned long max);
extern inline char rand_char(void);
extern int is_dot_entry(struct my_dirent *dirent);
extern int unlink_dirent(struct my_dirent *dirent);
extern void destroy_dir(void);
extern struct my_dirent *find_my_dirent(char *name);
extern void create_and_prep_dir(void);
extern void create_file(char *filename);
extern void create_files(char *prefix, int num);
extern int get_max_inlined_entries(int max_inline_size);
extern void get_directory_almost_full(int minus_this_many);
extern void random_unlink(int iters);
extern void random_fill_empty_entries(int iters);
extern void random_rename_same_reclen(int iters);
extern void random_deleting_rename(int iters);
extern void verify_dirents(void);
extern int is_dir_inlined(char *dirent_name, unsigned long *i_size,
			  unsigned int *id_count);
extern void should_inlined_or_not(int is_inlined, int should_inlined,
				  int test_no);
extern int open_ocfs2_volume(char *device_name);

static void usage(void)
{
	printf("Usage: inline-dirs [-i <iteration>] [-s operated_entries] "
	       "[-c <concurrent_process_num>] [-m <multi_file_num>] "
	       "<-d <device>> <mount_point>\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "dirs with inline data.\n\n"
	       "iteration specify the running times.\n"
	       "operated_dir_entries specify the entires number to be "
	       "operated,such as random create/unlink/rename.\n"
	       "concurrent_process_num specify the number of concurrent "
	       "multi_file_num specify the number of multiple dirs"
	       "processes to perform inline-data read/rename.\n"
	       "device and mount_point are mandatory.\n");
	exit(1);

}

static int parse_opts(int argc, char **argv)
{
	int c;
	while (1) {
		c = getopt(argc, argv, "D:d:I:i:C:c:M:m:S:s:");
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
		case 's':
		case 'S':
			operated_entries = atol(optarg);
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

/*
 * [I] Basic tests of inline-dir code.
 *    1) Basic add files
 *    2) Basic delete files
 *    3) Basic rename files
 *    4) Add / Remove / Re-add to fragment dir
 */
static void run_basic_tests(void)
{
	int ret;

	printf("Test %d: fill directory\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	testno++;

	printf("Test %d: expand inlined dir to extent exactly\n", testno);
	/*Should be 13 bits len dirent_name*/
	create_file("Iam13bitshere");
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	testno++;

	printf("Test %d: remove directory\n", testno);
	destroy_dir();
	testno++;

	printf("Test %d: rename files with same namelen\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_rename_same_reclen(operated_entries);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	destroy_dir();
	testno++;

	printf("Test %d: rename files with same namelen on top of each other\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_deleting_rename(operated_entries);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	destroy_dir();
	testno++;

	printf("Test %d: random unlink/fill entries.\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	random_unlink(operated_entries);
	random_fill_empty_entries(operated_entries);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	destroy_dir();
	testno++;

	printf("Test %d: random rename/unlink files with same namelen\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(operated_entries / 2);
	random_rename_same_reclen(operated_entries / 2);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	destroy_dir();
	testno++;

	printf("Test %d: fragment directory with unlinks/creates/renames\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(operated_entries / 2);
	random_rename_same_reclen(operated_entries / 2);
	create_files("frag1a", operated_entries / 2);
	random_unlink(operated_entries / 2);
	create_files("frag1b", operated_entries / 2);
	random_deleting_rename(operated_entries / 2);
	random_rename_same_reclen(operated_entries / 2);
	create_files("frag1c", operated_entries / 2);
	verify_dirents();
	destroy_dir();
	testno++;
}

/*
 * [II] Tests intended to push a dir out to extents
 *    1) Add enough files to push out one block
 *    2) Add enough files to push out two blocks
 *    3) Fragment dir, add enough files to push out to extents
 */
static void run_large_dir_tests(void)
{
	int ret;

	printf("Test %d: Add file name large enough to push out one block\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	create_files("Pushedfn-", 1);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	/*verify i_size should be one block size here*/
	if (i_size != blocksize) {
		fprintf(stderr, "i_size should be %d,while it's %lu here!\n",
			blocksize, i_size);
	}
	destroy_dir();
	testno++;

	printf("Test %d: Add file name large enough to push out two blocks\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	create_files("this_is_an_intentionally_long_filename_prefix_to_stress"
		     "_the_dir_code-this_is_an_intentionally_long_filename_pr"
		     "efix_to_stress_the_dir_code-this_is_an_intentionally_lo"
		     "ng_filename_prefix_to_stress_the_dir_code", 1);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	/*verify i_size should be one block size here*/
	if (i_size != blocksize * 2) {
		fprintf(stderr, "i_size should be %d,while it's %lu here!\n",
			blocksize * 2, i_size);
	}
	destroy_dir();
	testno++;

	printf("Test %d: fragment directory then push out to extents.\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(operated_entries / 2);
	random_rename_same_reclen(operated_entries / 2);
	create_files("frag2a", operated_entries / 2);
	random_unlink(operated_entries / 2);
	create_files("frag2b", operated_entries / 2);
	random_deleting_rename(operated_entries / 2);
	random_rename_same_reclen(operated_entries / 2);
	create_files("frag2c", operated_entries / 2 + 1);
	create_files("frag2d", operated_entries / 2 + 1);
	verify_dirents();
	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 0, testno);
	destroy_dir();
	testno++;
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
	int process_nums;

	if (do_multi_process_test)
		process_nums = child_nums;
	else
		process_nums = file_nums;

	for (i = 0; i < process_nums; i++)
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

static int set_semvalue(void)
{
	union semun sem_union;

	sem_union.val = 1;
	if (semctl(sem_id, 0, SETVAL, sem_union) == -1) {
		perror("semctl");
		return -1;
	}

	return 0;
}

static int semaphore_p(void)
{
	struct sembuf sem_b;

	sem_b.sem_num = 0;
	sem_b.sem_op = -1; /* P() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(sem_id, &sem_b, 1) == -1) {
		fprintf(stderr, "semaphore_p failed\n");
		return -1;
	}

	return 0;
}

static int semaphore_v(void)
{
	struct sembuf sem_b;

	sem_b.sem_num = 0;
	sem_b.sem_op = 1; /* V() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(sem_id, &sem_b, 1) == -1) {
		fprintf(stderr, "semaphore_v failed\n");
		return -1;
	}

	return 0;
}

static void del_semvalue(void)
{
	union semun sem_union;

	if (semctl(sem_id, 0, IPC_RMID, sem_union) == -1)
		fprintf(stderr, "Failed to delete semaphore\n");
}

static void run_concurrent_test(void)
{
	int ret, rc;
	int i, status;
	struct my_dirent *old_dirents;
	key_t sem_key = IPC_PRIVATE, shm_key = IPC_PRIVATE;

	pid_t pid;

	if (!do_multi_process_test)
		return;

	printf("Test %d: concurrent dir RW with multiple processes!\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(1);

	/*get and init semaphore*/
	sem_id = semget(sem_key, 1, 0766 | IPC_CREAT);
	if (set_semvalue() < 0) {
		fprintf(stderr, "Set semaphore value failed!\n");
		exit(1);
	}

	/*should use shared memory here */
	old_dirents = dirents;
	dirents = NULL;

	shm_id = shmget(shm_key, sizeof(struct my_dirent) * MAX_DIRENTS,
			IPC_CREAT | 0766);
	if (shm_id < 0) {
		perror("shmget");
		exit(1);
	}

	dirents = (struct my_dirent *)shmat(shm_id, 0, 0);
	if (dirents < 0) {
		perror("shmat");
		exit(1);
	}

	shmctl(shm_id, IPC_RMID, 0);
	memmove(dirents, old_dirents, sizeof(struct my_dirent) * MAX_DIRENTS);

	/*flush out the father's i/o buffer*/
	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	for (i = 0; i < child_nums; i++) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			exit(pid);
		}
		if (pid == 0) {
			if (semaphore_p() < 0)
				exit(-1);
			/*Concurrent rename for dirents*/
			random_rename_same_reclen(operated_entries);
			if (semaphore_v() < 0)
				exit(-1);
			/*child exits normally*/
			sleep(1);
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
			exit(rc);
		}
	}
	/*father help to verfiy dirents' consistency*/
	sleep(2);
	verify_dirents();

	ret = is_dir_inlined(dirent_name, &i_size, &id_count);
	should_inlined_or_not(ret, 1, testno);
	destroy_dir();

	/*detach shared memory*/
	if (shmdt(dirents) == -1) {
		perror("shmdt");
		 exit(1);
	}

	dirents = old_dirents;
	testno++;

	return;
}

static void run_multiple_test(void)
{
	int i,  status;

	pid_t pid;
	int ret, rc;

	if (!do_multi_file_test)
		return;

	printf("Test %d: multiple dirs RW with multiple processes!\n", testno);

	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	for (i = 0; i < file_nums; i++) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			exit(pid);
		}
		if (pid == 0) {
			snprintf(dirent_name, OCFS2_MAX_FILENAME_LEN,
				 "inline-data-dir-test-%d", getpid());
			snprintf(dir_name, OCFS2_MAX_FILENAME_LEN, "%s/%s",
				 work_place, dirent_name);
			create_and_prep_dir();
			get_directory_almost_full(1);
			random_rename_same_reclen(operated_entries);
			random_unlink(operated_entries);
			random_fill_empty_entries(operated_entries);
			verify_dirents();
			sync();
			sleep(1);
			ret = is_dir_inlined(dirent_name, &i_size, &id_count);
			should_inlined_or_not(ret, 1, testno);
			destroy_dir();
			exit(0);

		}
		if (pid > 0)
			child_pid_list[i] = pid;

	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	/*father wait all children to leave*/
	for (i = 0; i < file_nums; i++) {
		ret = waitpid(child_pid_list[i], &status, 0);
		rc = WEXITSTATUS(status);
		if (rc) {
			fprintf(stderr, "Child %d exits abnormally with "
				"RC=%d\n", child_pid_list[i], rc);
			exit(rc);
		}
	}

	testno++;
	return;
}

static void setup(int argc, char *argv[])
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
		exit(ret);
	}

	if (do_multi_process_test)
		child_pid_list = (pid_t *)malloc(sizeof(pid_t) * child_nums);
	if (do_multi_file_test)
		child_pid_list = (pid_t *)malloc(sizeof(pid_t) * file_nums);

	dirents = (struct my_dirent *)malloc(sizeof(struct my_dirent) *
					     MAX_DIRENTS);
	memset(dirents, 0, sizeof(struct my_dirent) * MAX_DIRENTS);

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);

	snprintf(work_place, OCFS2_MAX_FILENAME_LEN, "%s/%s", mount_point,
		 WORK_PLACE);
	mkdir(work_place, FILE_MODE);

	snprintf(dirent_name, 255, "inline-data-dir-test");
	snprintf(dir_name, 255, "%s/%s", work_place, dirent_name);

	printf("BlockSize:\t\t%d\nMax Inline Data Size:\t%d\n"
	       "ClusterSize:\t\t%lu\nPageSize:\t\t%lu\nWorkingPlace:\t\t%s\n"
	       "NumOfMaxInlinedEntries:\t\t%d\n\n", blocksize, max_inline_size,
	       clustersize, page_size, work_place,
	       get_max_inlined_entries(max_inline_size));

	return;

}

static void teardown(void)
{
	if (dirents)
		free(dirents);

	if (child_pid_list)
		free(child_pid_list);
}

int main(int argc, char **argv)
{
	int i;

	setup(argc, argv);

	for (i = 0; i < iteration; i++) {

		printf("################Test Round %d################\n", i);
		testno = 1;
		run_basic_tests();
		run_large_dir_tests();
		run_multiple_test();
		run_concurrent_test();
		printf("All File I/O Tests Passed\n");

	}

	teardown();

	return 0;
}
