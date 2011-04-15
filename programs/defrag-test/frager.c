/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * frager.c
 *
 * A simple utility to frag the filesystem on purpose.
 *
 * boost the disk defragmenation by spawning massive
 * processes with growing writes, to simulate the interleaving
 * writes from users over time in a real world.
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <inttypes.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <ocfs2/ocfs2.h>

#include "file_verify.h"

#define FILE_FLAGS		(O_CREAT|O_RDWR|O_APPEND)

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

char *work_place = NULL, *log_place = NULL;
char hostname[256];
unsigned long num_files = 1000;
unsigned long num_processes = 10;
unsigned long file_size = 1024 * 1024;
unsigned long chunk_size = 32 * 1024;
int is_random = 0;
int verbose = 0;
int do_refcount = 0;
union log_handler w_log;

pid_t *child_pid_list;

static int usage(void)
{
	fprintf(stdout, "frager <-n num_files_per_process> <-m num_processes> "
		"<-l file_size> <-k chunk_size> <-o logfiles_place> <-r> <-v>"
		" <-w work_place> [-R]\n");
	fprintf(stdout, "Example:\n"
			"       ./frager -n 10 -m 10 -l 104857600 -k 32768 -o "
		"logs -w /storage\n");
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

int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv, "n:m:w:hk:rRl:vo:");
		if (c == -1)
			break;

		switch (c) {
		case 'w':
			work_place = optarg;
			break;
		case 'n':
			num_files = atol(optarg);
			break;
		case 'm':
			num_processes = atol(optarg);
			break;
		case 'l':
			file_size = atol(optarg);
			break;
		case 'k':
			chunk_size = atol(optarg);
			break;
		case 'o':
			log_place = optarg;
			break;
		case 'r':
			is_random = 1;
			break;
		case 'R':
			do_refcount = 1;
			break;
		case 'v':
			verbose = 1;
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

int setup(int argc, char *argv[])
{
	int ret = 0;

	work_place = NULL;
	is_random = 0;

	if (parse_opts(argc, argv))
		usage();

	if ((!work_place) || (!log_place)) {
		fprintf(stderr, "work_place and log_place is a mandatory"
			" option.\n");
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

	return ret;
}

void teardown()
{
	if (child_pid_list)
		free(child_pid_list);
}

int write_at(int fd, const void *buf, size_t count, off_t offset)
{
	int ret;

	size_t bytes_write;

	ret = pwrite(fd, buf, count, offset);

	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "write error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	bytes_write = ret;
	while (bytes_write < count) {

		ret = pwrite(fd, buf + bytes_write, count - bytes_write,
			     offset + bytes_write);

		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "write error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}

		bytes_write += ret;
	}

	return 0;
}

unsigned long get_rand_ul(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

int reflink(const char *oldpath, const char *newpath)
{
	int fd, ret;
	struct reflink_arguments args;

	args.old_path = (__u64)oldpath;
	args.new_path = (__u64)newpath;

	fd = open64(oldpath, O_RDONLY);

	if (fd < 0) {
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", oldpath, fd,
			strerror(fd));
		return fd;
	}

	ret = ioctl(fd, OCFS2_IOC_REFLINK, &args);

	if (ret) {
		ret = errno;
		fprintf(stderr, "ioctl failed:%d:%s\n", ret, strerror(ret));
		return ret;
	}

	close(fd);

	return 0;
}

int append_write_file(char *file_name, char *logfile_name,
		      unsigned long file_size, unsigned long chunk_size,
		      int flags, int verbose)
{
	int fd = -1, ret = 0;
	unsigned long num_chunks, chunk_no, i;
	unsigned long index, *write_order_map = NULL;
	char *pattern = NULL;
	static struct write_unit wu;
	FILE *logfile = NULL;

	num_chunks = (file_size + chunk_size - 1) / chunk_size;

	pattern = (char *)malloc(chunk_size);
	write_order_map = (unsigned long *)malloc(num_chunks *
						  sizeof(unsigned long));
	memset(write_order_map, 0, num_chunks * sizeof(unsigned long));

	if (!is_random) {
		for (i = 0; i < num_chunks; i++)
			write_order_map[i] = i;
	} else {
		/*
		 * in random mode, chunks within file will be filled
		 * up randomly, it however guarantees all chunks get
		 * written.
		 */
		write_order_map[0] = 0;

		for (i = 1; i < num_chunks; i++) {
again:
			index = get_rand_ul(1, num_chunks - 1);
			if (!write_order_map[index])
				write_order_map[index] = i;
			else
				goto again;
		}
	}

	fd = open64(file_name, flags, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, ret,
			strerror(ret));
		ret = fd;
		goto bail;
	}

	ret = open_logfile(&logfile, logfile_name, 0);
	if (ret)
		goto bail;

	w_log.stream_log = logfile;

	for (i = 0; i < num_chunks; i++) {		

		chunk_no = write_order_map[i];
		prep_rand_dest_write_unit(&wu, chunk_no, chunk_size);
		/*
		 * each chunk is written in a unique pattern, which helps
		 * making the following verfication more accurate and easier.
		 */
		fill_chunk_pattern(pattern, &wu);

		if (verbose)
			fprintf(stdout, "Process %d writing #%lu chunk to "
				"file %s\n", getpid(), wu.wu_chunk_no, file_name);

		ret = do_write_chunk(fd, wu);
		if (ret < 0)
			goto bail;

		/*
		 * writes the log records into local logfile.
		 */
		ret = log_write(&wu, w_log, 0);
		if (ret < 0)
			goto bail;
	}

bail:
	if (pattern)
		free(pattern);

	if (write_order_map)
		free(write_order_map);

	if (fd > 0)
		close(fd);

	if (w_log.stream_log)
		fclose(w_log.stream_log);

	return ret;
}

int write_files(unsigned long long files,
		 unsigned long long processes,
		 const char *dir, const char *log_dir)
{
	int status = 0, ret = 0;
	pid_t pid;
	unsigned int seed;
	unsigned long long i, j;
	char path[PATH_MAX], log_path[PATH_MAX], ref_path[PATH_MAX];

	for (i = 0; i < processes; i++) {

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "fork error:%s\n", strerror(pid));
			teardown();
			exit(pid);
		}

		if (pid == 0) {

			if (is_random) {
				seed = time(NULL) ^ getpid();
				srandom(seed);
			}

			snprintf(path, PATH_MAX, "%s/%s-%d", dir, hostname,
				 getpid());

			snprintf(log_path, PATH_MAX, "%s/%s-%d",
				 log_dir, hostname, getpid());

			ret = mkdir(path, FILE_MODE);
			if (ret < 0) {
				fprintf(stderr, "%d failed to mkdir %s: %s\n",
					getpid(), path, strerror(errno));
				teardown();
				exit(ret);
			}

			ret = mkdir(log_path, FILE_MODE);
			if (ret < 0) {
				fprintf(stderr, "%d failed to mkdir(log) %s: "
					"%s\n", getpid(), log_path, strerror(errno));
				teardown();
				exit(ret);
			}

			for (j = 0; j < files; j++) {
				snprintf(path, PATH_MAX, "%s/%s-%d/file-%llu",
					 dir, hostname, getpid(), j);
				snprintf(log_path, PATH_MAX, "%s/%s-%d/logfile-%llu",
					 log_dir, hostname, getpid(), j);

				ret = append_write_file(path, log_path,
							file_size, chunk_size,
							FILE_FLAGS, verbose);
				if (ret < 0) {
					teardown();
					exit(ret);
				}

				if (do_refcount) {
					snprintf(ref_path, PATH_MAX, "%s/%s-%d/refile-%llu",
						 dir, hostname, getpid(), j);
					ret = reflink(path, ref_path);
					if (ret < 0) {
						teardown();
						exit(ret);
					}
				}
			}

			teardown();
			exit(0);
		}

		if (pid > 0)
			child_pid_list[i] = pid;
	}

	/*
	 * We're only going to setup following signals
	 * for father to avoid zombies and cleaup children.
	 */
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigterm_handler);

	for (i = 0; i < num_processes; i++)
		ret = waitpid(child_pid_list[i], &status, 0);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	ret = setup(argc, argv);
	if (ret)
		return ret;

	ret = write_files(num_files, num_processes, work_place, log_place);

	teardown();

	return ret;
}
