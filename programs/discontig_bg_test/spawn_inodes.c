/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * spawn_inodes.c
 *
 * A simple utility to spawn inodes with multiple processes
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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
 */

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <inttypes.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define FILE_FLAGS		(O_CREAT|O_RDWR|O_APPEND|O_TRUNC)

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char *workplace;
char hostname[256];
unsigned long long num_inodes = 1000;
unsigned long long num_processes = 10;
int is_random;

pid_t *child_pid_list;

static int usage(void)
{
	fprintf(stdout, "spawn_inodes <-n num_inodes> <-m num_processes> "
		"<-w work_place> [-r]\n");
	exit(1);
}

static void sigchld_handler()
{
	pid_t   pid;
	union wait status;

	while (1) {
		pid = wait3(&status, WNOHANG, NULL);
		if (pid <= 0)
			break;
	}
}

static void kill_all_children()
{
	unsigned long long i;

	for (i = 0; i < num_processes; i++)
		if (child_pid_list[i])
			kill(child_pid_list[i], SIGTERM);
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

static void atexit_hook(void)
{
	unsigned long long i;

	for (i = 0; i < num_processes; i++)
		if (child_pid_list[i])
			kill(child_pid_list[i], SIGKILL);

	if (child_pid_list)
		free(child_pid_list);
}

int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv, "n:m:w:rh:");
		if (c == -1)
			break;

		switch (c) {
		case 'w':
			workplace = optarg;
			break;
		case 'n':
			num_inodes = atoll(optarg);
			break;
		case 'm':
			num_processes = atoll(optarg);
			break;
		case 'r':
			is_random = 1;
			break;
		case 'h':
			usage();
			break;
		default:
			return -1;
		}
	}

	return 0;
}

void setup(int argc, char *argv[])
{
	workplace = NULL;
	is_random = 0;

	if (parse_opts(argc, argv))
		usage();

	if (!workplace) {
		fprintf(stderr, "workplace is a mandatory option.\n");
		usage();
	}

	if (gethostname(hostname, 256) < 0) {
		fprintf(stderr, "gethostname failed.\n");
		exit(1);
	}

	child_pid_list = (pid_t *)malloc(sizeof(pid_t) * num_processes);

	memset(child_pid_list, 0, sizeof(pid_t) * num_processes);

	/*
	 * Setup SIGCHLD correctly to avoid zombie processes.
	 */
	signal(SIGCHLD, sigchld_handler);
}

void teardown()
{
	if (child_pid_list)
		free(child_pid_list);
}

int spawn_inodes(unsigned long long inodes,
		 unsigned long long processes,
		 const char *dir)
{
	int status = 0, ret = 0, fd;
	pid_t pid;
	unsigned long long i, j;
	unsigned int seed;
	char path[PATH_MAX];

	for (i = 0; i < processes; i++) {

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "fork error:%s\n", strerror(pid));
			teardown();
			exit(pid);
		}

		if (pid == 0) {
			snprintf(path, PATH_MAX, "%s/%s-%d", dir, hostname,
				 getpid());

			ret = mkdir(path, FILE_MODE);
			if (ret < 0) {
				fprintf(stderr, "%d failed to mkdir %s: %s\n",
					getpid(), path, strerror(errno));
				teardown();
				exit(ret);
			}

			for (j = 0; j < inodes; j++) {
				snprintf(path, PATH_MAX, "%s/%s-%d/inodes-%d-%llu",
					 dir, hostname, getpid(), getpid(), j);

				fd = open(path, FILE_FLAGS, FILE_MODE);
				if (fd < 0) {
					fprintf(stderr, "%d failed to create "
						"file %s: %s\n", getpid(), path,
						strerror(errno));
					teardown();
					exit(fd);
				}

				if (!is_random) {
					close(fd);
					continue;
				}

				seed = time(NULL) ^ getpid();
				srandom(seed);

				if (rand() % 2) {
					ret = unlink(path);
					if (ret < 0) {
						fprintf(stderr, "%d failed to"
							" unlink %s: %s.\n",
							getpid(), path,
							strerror(errno));
						teardown();
						exit(ret);
					}
				}

				if (!(rand() % 2)) {
					ret = write(fd, "be-inlined", 100);
					if (ret < 0) {
						fprintf(stderr, "%d failed to"
							" write %s: %s.\n",
							getpid(), path,
							strerror(errno));
						teardown();
						exit(ret);
					}
				}

				close(fd);
			}

			teardown();
			exit(0);
		}

		if (pid > 0)
			child_pid_list[i] = pid;
	}

	/*
	 * We're only going to setup following signals
	 * for father to avoid zombies and cleaup children
	 */
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	atexit(atexit_hook);

	for (i = 0; i < num_processes; i++)
		ret = waitpid(child_pid_list[i], &status, 0);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	setup(argc, argv);

	ret = spawn_inodes(num_inodes, num_processes, workplace);

	return ret;
}
