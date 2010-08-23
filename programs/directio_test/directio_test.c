/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * directio_test.c
 *
 * It's a generic tool to test O_DIRECT in following cases:
 *
 *   - In-place writes within i_size.
 *   - Apend writes outside i_size.
 *   - Writes within holes.
 *   - Destructive test.
 *
 * XXX: This could easily be turned into an mpi program.
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

int open_rw_flags = FILE_RW_FLAGS;
int open_ro_flags = FILE_RO_FLAGS;

unsigned long file_size = 1024 * 1024;

static char *test_mode;

static char workfile[PATH_MAX];
static char log_path[PATH_MAX];
static char lsnr_addr[HOSTNAME_LEN];

static union log_handler log;

static unsigned long port = 9999;
static unsigned long num_children = 10;

static pid_t *child_pid_list;

int test_flags = 0x00000000;
int verbose = 0;

int num_tests = 0;

static void usage(void)
{
	printf("Usage: directio_test [-p concurrent_process] "
	       "[-l file_size] [-o logfile] <-w workfile>  -b -a -f "
	       "[-d <-A listener_addres> <-P listen_port>] -v -V\n"
	       "file_size should be multiples of 512 bytes\n"
	       "-v enable verbose mode."
	       "-b enable basic directio test within i_size.\n"
	       "-a enable append write test.\n"
	       "-f enable fill hole test.\n"
	       "-d enable destructive test, also need to specify the "
	       "listener address and port\n"
	       "-V enable verification test.\n\n");
	exit(1);
}

static int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv,
			   "p:l:o:bafdVvw:h:A:P:");
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			num_children = atol(optarg);
			break;
		case 'l':
			file_size = atol(optarg);
			break;
		case 'o':
			strcpy(log_path, optarg);
			break;
		case 'w':
			strcpy(workfile, optarg);
			break;
		case 'b':
			test_flags |= BASC_TEST;
			test_mode = "BASIC";
			num_tests++;
			break;
		case 'a':
			test_flags |= APPD_TEST;
			test_mode = "APPEND";
			num_tests++;
			break;
		case 'f':
			test_flags |= FIHL_TEST;
			test_mode = "HOLE_FILLING";
			num_tests++;
			break;
		case 'd':
			test_flags |= DSCV_TEST;
			test_mode = "DESTRUCTIVE";
			/*
			 * Destructive test needs append writing mode.
			 */
			test_flags |= APPD_TEST;
			num_tests++;
			break;
		case 'V':
			test_flags |= VERI_TEST;
			test_mode = "VERIFY";
			num_tests++;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'A':
			strcpy(lsnr_addr, optarg);
			break;
		case 'P':
			port = atol(optarg);
			break;
		case 'h':
			usage();
			break;
		default:
			break;
		}
	}

	if (strcmp(workfile, "") == 0)
		return -EINVAL;

	if (test_flags & DSCV_TEST) {
		if (!strcmp(lsnr_addr, ""))
			return -EINVAL;
	}

	if ((file_size % DIRECTIO_SLICE) != 0) {
		fprintf(stderr, "file size in destructive tests is expected to "
			"be %d aligned, your file size %lu is not allowed.\n",
			CHUNK_SIZE, file_size);
		return -EINVAL;
	}

	if (!num_tests) {
		fprintf(stdout, "You'd better specify at least one test.\n");
		return 0;
	}

	if (num_tests > 1) {
		fprintf(stdout, "You have to specify ONLY ONE test a time.\n");
		return -EINVAL;
	}

	return 0;
}

static int setup(int argc, char *argv[])
{
	int ret = 0, sockfd;
	FILE *logfile = NULL;

	if (parse_opts(argc, argv))
		usage();

	if (test_flags & DSCV_TEST) {

		sockfd = init_sock(lsnr_addr, port);
		if (sockfd < 0) {
			fprintf(stderr, "init socket failed.\n");
			return sockfd;
		}

		log.socket_log = sockfd;	

	} else {
		ret = open_logfile(&logfile, log_path);
		if (ret)
			return ret;

		log.stream_log = logfile;
	}

	child_pid_list = (pid_t *)malloc(sizeof(pid_t) * num_children);

	return ret;
}

static int teardown(void)
{
	int ret = 0;

	if (test_flags & DSCV_TEST) {
		if (log.socket_log)
			close(log.socket_log);
	} else {
		if (log.stream_log)
			fclose(log.stream_log);
	}

	if (child_pid_list)
		free(child_pid_list);

	return ret;
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

	for (i = 0; i < num_children; i++)
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

static int basic_test(void)
{
	pid_t pid;
	int fd, sem_id, status, ret = 0;
	unsigned long i, j, chunk_no = 0, num_chunks = 0;
	struct write_unit wu;

	sem_id = semaphore_init(1);
	if (sem_id < 0)
		return sem_id;

	num_chunks = file_size / CHUNK_SIZE;

	open_rw_flags |= O_DIRECT;
	open_ro_flags |= O_DIRECT;

	if (test_flags & BASC_TEST) {
		fprintf(stdout, "# Prepare file in %lu length.\n", file_size);
		ret = prep_orig_file_in_chunks(workfile, file_size);
		if (ret)
			return ret;
	}

	fflush(stderr);
	fflush(stdout);

	signal(SIGCHLD, sigchld_handler);

	fd = open_file(workfile, open_rw_flags);
	if (fd < 0)
		return fd;

	if (test_flags & FIHL_TEST) {
		fprintf(stdout, "# Reserve a hole by truncating file to %lu.\n",
			file_size);
		ret = ftruncate(fd, file_size);
		if (ret) {
			ret = errno;
			fprintf(stderr, "ftruncate faile:%d,%s\n",
				ret, strerror(ret));
			return ret;
		}
	}

	fprintf(stdout, "# Fork %lu processes performing writes.\n",
		num_children);
	for (i = 0; i < num_children; i++) {

		pid = fork();

		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			return pid;
		}

		if (pid == 0) {

			srand(getpid());

			for (j = 0; j < num_chunks; j++) {
				if (verbose) 
					fprintf(stdout, "  #%d process writes "
						"#%lu chunk\n", getpid(),
						chunk_no);

				if (semaphore_p(sem_id) < 0) {
					ret = -1;
					goto child_bail;
				}

				if (test_flags & APPD_TEST)
					chunk_no = j;
				else
					chunk_no = get_rand_ul(0, num_chunks - 1);

				prep_rand_dest_write_unit(&wu, chunk_no);

				ret = do_write_chunk(fd, wu);
				if (ret < 0)
					goto child_bail;

				ret = log_write(&wu, log);
				if (ret < 0)
					goto child_bail;

				if (semaphore_v(sem_id) < 0) {
					ret = -1;
					goto child_bail;
				}

				usleep(10000);

				if (!(test_flags & DSCV_TEST))
					continue;

				/*
				 * Are you ready to crash the machine?
				 */

				if ((j > 1) && (j < num_chunks - 1)) {
					if (get_rand_ul(1, num_chunks) == num_chunks / 2) {

						if (semaphore_p(sem_id) < 0) {
							ret = -1;
							goto child_bail;
						}

						fprintf(stdout, "#%d process "
							"tries to crash the "
							"box.\n", getpid());
						system("echo b>/proc/sysrq-trigger");
					}
				} else if (j == num_chunks - 1) {

						if (semaphore_p(sem_id) < 0) {
							ret = -1;
							goto child_bail;
						}

						fprintf(stdout, "#%d process "
							"tries to crash the "
							"box.\n", getpid());
						system("echo b>/proc/sysrq-trigger");
				}
			}
child_bail:
			if (fd)
				close(fd);

			if (ret > 0)
				ret = 0;

			exit(ret);
		}

		if (pid > 0)
			child_pid_list[i] = pid;
	}

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	for (i = 0; i < num_children; i++) {
		waitpid(child_pid_list[i], &status, 0);
		ret = WEXITSTATUS(status);
		if (ret) {
			fprintf(stderr, "Child %d exits abnormally with "
				"RC=%d\n", child_pid_list[i], ret);
		}
	}

	if (fd)
		close(fd);

	if (sem_id)
		semaphore_close(sem_id);

	return ret;
}

static int verify_test(void)
{
	int ret = 0;

	ret = 

	fprintf(stdout, "# Verify file %s in chunks\n", workfile);
	ret = verify_file(log.stream_log, workfile, file_size);

	return ret;
}

static int run_test(void)
{
	int ret = 0;

		
	if (!test_flags)
		return 0;

	fprintf(stdout, "########## %s test running ##########\n", test_mode);

	if (test_flags & VERI_TEST) {
		ret = verify_test();
		if (ret < 0)
			return ret;
	} else {
		ret = basic_test();
		if (ret)
			return ret;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = setup(argc, argv);
	if (ret)
		return ret;

	ret = run_test();
	if (ret)
		return ret;

	ret = teardown();

	return ret;
}
