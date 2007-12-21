/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <linux/types.h>

#include "mpi.h"

#define HOSTNAME_SIZE 50
static char hostname[HOSTNAME_SIZE];
static int rank = -1, num_procs;
static int use_flock = 1;
static char *path1, *path2, *prog;

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void info(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);
}

static int lock_fcntl(char *fname, int fd, int ex, int try)
{
	int ret = 0;
	struct flock fl;
	char *lkstr;
	char *cmdstr;
	int cmd;

	memset(&fl, 0, sizeof(fl));
	if (ex) {
		lkstr = "F_WRLCK";
		fl.l_type = F_WRLCK;
	} else {
		lkstr = "F_RDLCK";
		fl.l_type = F_RDLCK;
	}
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (try) {
		cmdstr = "F_SETLK";
		cmd = F_SETLK;
	} else {
		cmdstr = "F_SETLKW";
		cmd = F_SETLKW;
	}
	info("fcntl(%s, %s, {%s, SEEK_SET, 0, 0})\n", fname, cmdstr, lkstr);

	ret = fcntl(fd, cmd, &fl);
	if (ret == -1 && try && (errno == EAGAIN || errno == EACCES)) {
		/*
		 * fake a consistent error return for a failed
		 * trylock.
		 */
		ret = EWOULDBLOCK;
	} else if (ret == -1 && errno == EINTR) {
		ret = EINTR;
	} else if (ret == -1) {
		ret = errno;
		abort_printf("error %d locking %s: %s\n",
			     ret, fname, strerror(ret));
	}
	return ret;
}

static int lock_flock(char *fname, int fd, int ex, int try)
{
	int ret = 0;
	int level = LOCK_EX;
	char *lkstr = "LOCK_EX";
	char *trystr = "";

	if (ex == 0) {
		lkstr = "LOCK_SH";
		level = LOCK_SH;
	}
	if (try) {
		trystr = "|LOCK_NB";
		level |= LOCK_NB;
	}

	info("flock(%s, %s%s)\n", fname, lkstr, trystr);

	ret = flock(fd, level);
	if (ret == -1 && try && errno == EWOULDBLOCK) {
		ret = EWOULDBLOCK;
	} else if (ret == -1 && errno == EINTR) {
		ret = EINTR;
	} else if (ret == -1) {
		ret = errno;
		abort_printf("error %d locking %s: %s\n",
			     ret, fname, strerror(ret));
	}
	return ret;
}

static int lock(char *fname, int fd, int ex, int try)
{
	if (use_flock)
		return lock_flock(fname, fd, ex, try);
	return lock_fcntl(fname, fd, ex, try);
}

static void lock_abort(char *fname, int fd, int expected_return, int ex,
		       int try)
{
	int ret;

	ret = lock(fname, fd, ex, try);
	if (ret == -1)
		ret = errno;
	if (ret != expected_return) {
		abort_printf("Lock failed, expected %d (\"%s\"), got: %d "
			     "(\"%s\")\n",
			     expected_return, strerror(expected_return), ret,
			     strerror(ret));
	}
}

static int unlock_fcntl(char *fname, int fd)
{
	int ret = 0;
	struct flock fl;

	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	info("fcntl(%s, F_SETLKW, {F_UNLCK, SEEK_SET, 0, 0})\n", fname);

	ret = fcntl(fd, F_SETLKW, &fl);
	if (ret == -1) {
		ret = errno;
		abort_printf("error %d locking %s: %s\n",
			     ret, fname, strerror(ret));
	}
	return ret;
}

static int unlock_flock(char *fname, int fd)
{
	int ret = 0;

	info("flock(%s, LOCK_UN)\n", fname);

	ret = flock(fd, LOCK_UN);
	if (ret == -1) {
		ret = errno;
		abort_printf("error %d locking %s: %s\n",
			     ret, fname, strerror(ret));
	}
	return ret;
}

static int unlock(char *fname, int fd)
{
	if (use_flock)
		return unlock_flock(fname, fd);
	return unlock_fcntl(fname, fd);
}

static void unlock_abort(char *fname, int fd, int expected_return)
{
	int ret;

	ret = unlock(fname, fd);
	if (ret == -1)
		ret = errno;
	if (ret != expected_return) {
		abort_printf("Unlock failed, expected %d (\"%s\"), got: %d "
			     "(\"%s\")\n",
			     expected_return, strerror(expected_return), ret,
			     strerror(ret));
	}
}


static int change_locking_type(char *type)
{
	if (strcmp(type, "fcntl") == 0) {
		use_flock = 0;
		return 0;
	} else if (strcmp(type, "flock") == 0) {
		use_flock = 1;
		return 0;
	}

	return EINVAL;
}

static void usage(void)
{
	printf("usage: %s [-f <type>] <file1> <file2>\n", prog);
	printf("<type> defaults to \"flock\" but can also be \"fcntl\".\n");
	printf("<file1>, <file2> are required.\n");
	printf("This will test the functionality of user file locking\n");
	printf("within a cluster. Two processes are required.\n");

	MPI_Finalize();
	exit(1);
}

int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "t:");
		if (c == -1)
			break;

		switch (c) {
		case 't':
			if (change_locking_type(optarg))
				return EINVAL;
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 2)
		return EINVAL;

	path1 = argv[optind];
	path2 = argv[optind + 1];

	return 0;
}

static void signal_handler(int sig)
{
	info("signal %d recieved\n", sig);
}

static void set_alarm(int seconds)
{
	int ret;
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	act.sa_flags = SA_RESETHAND;
	if (sigaction(SIGALRM, &act, NULL) == -1)
		abort_printf("Couldn't setup SIGALRM handler!\n");

	ret = alarm(seconds);
	if (ret)
		abort_printf("failure %d while setting up alarm: \"%s\"\n", ret,
			     strerror(ret));
}

static int get_fd(char *fname)
{
	int fd;

	fd = open(fname, O_RDWR);
	if (fd == -1) {
		fd = errno;
		abort_printf("open error %d: \"%s\"\n", fd, strerror(fd));
	}

	return fd;
}

#define do_barrier() __do_barrier(__LINE__)
static void __do_barrier(unsigned int lineno)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Barrier at line %u failed: %d\n", lineno, ret);
}

/*
 * Open two fds on the same file and take sh locks on each fd.
 */
static void test_1node_1file(char *fname)
{
	int fd1, fd2;

	fd1 = get_fd(fname);
	fd2 = get_fd(fname);

	info("*** Single node, same file, multiple fds tests ***\n");

	info("Test two shared locks\n");
	lock_abort(fname, fd1, 0, 0, 0);
	lock_abort(fname, fd2, 0, 0, 0);
	unlock_abort(fname, fd1, 0);
	unlock_abort(fname, fd2, 0);


	info("Two exclusive trylocks\n");
	lock_abort(fname, fd1, 0, 1, 1);
	lock_abort(fname, fd2, EWOULDBLOCK, 1, 1);
	unlock_abort(fname, fd1, 0);


	info("One exclusive, one shared trylock\n");
	lock_abort(fname, fd1, 0, 1, 0);
	lock_abort(fname, fd2, EWOULDBLOCK, 0, 1);
	unlock_abort(fname, fd1, 0);


	info("One shared, upconvert to exclusive\n");
	lock_abort(fname, fd1, 0, 0, 0);
	lock_abort(fname, fd1, 0, 1, 0);
	unlock_abort(fname, fd1, 0);


	info("One shared, taken twice in a row\n");
	lock_abort(fname, fd1, 0, 0, 0);
	lock_abort(fname, fd1, 0, 0, 0);
	unlock_abort(fname, fd1, 0);


	info("One exclusive, taken twice in a row\n");
	lock_abort(fname, fd1, 0, 1, 0);
	lock_abort(fname, fd1, 0, 1, 0);
	unlock_abort(fname, fd1, 0);


	info("One exclusive, taken twice in a row, 2nd is a trylock\n");
	lock_abort(fname, fd1, 0, 1, 0);
	lock_abort(fname, fd1, 0, 1, 1);
	unlock_abort(fname, fd1, 0);

	info("*** This section of tests passed ***\n");

	close(fd1);
	close(fd2);
}

static void test_2nodes_1file(char *fname, int master)
{
	int fd;

	fd = get_fd(fname);

	info("*** Two nodes, same file, multiple fds tests ***\n");


	info("Test two shared locks\n");
	lock_abort(fname, fd, 0, 0, 0);
	do_barrier();
	unlock_abort(fname, fd, 0);

	do_barrier();

	info("Two exclusive trylocks\n");
	if (master) {
		lock_abort(fname, fd, 0, 1, 1);
		/*
		 * 1st barrier signals we got the lock, 2nd one waits
		 * for other node to trylock.
		 */
		do_barrier();
		do_barrier();
		unlock_abort(fname, fd, 0);
	} else {
		do_barrier();
		lock_abort(fname, fd, EWOULDBLOCK, 1, 1);
		do_barrier();
	}

	info("One exclusive, one shared trylock\n");
	if (master) {
		lock_abort(fname, fd, 0, 1, 0);
		do_barrier();
		do_barrier();
		unlock_abort(fname, fd, 0);
	} else {
		do_barrier();
		lock_abort(fname, fd, EWOULDBLOCK, 0, 1);
		do_barrier();
	}

	info("*** This section of tests passed ***\n");

	close(fd);
}

static void test_deadlocks(char *fname1, char *fname2, int master)
{
	int fd1, fd2;

	fd1 = get_fd(fname1);
	fd2 = get_fd(fname2);

	info("*** Two nodes deadlock tests ***\n");

	info("Rank 0 takes lock, rank 1 waits on it but gets signal after hanging for 10 seconds\n");
	if (master) {
		lock_abort(fname1, fd1, 0, 1, 0);
		do_barrier();
		/* let other process hang and abort lock request */
		do_barrier();
		unlock_abort(fname1, fd1, 0);
	} else {
		do_barrier();
		set_alarm(10);
		lock_abort(fname1, fd1, EINTR, 1, 0);
		do_barrier();
	}

	info("*** This section of tests passed ***\n");

	close(fd1);
	close(fd2);
}

int main(int argc, char *argv[])
{
	int ret;
	char *which;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Init failed: %d\n", ret);
		exit(1);
	}

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

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

	if (num_procs != 2)
		abort_printf("Need two processes for this test.\n");

        printf("%s: rank: %d, procs: %d, path1 \"%s\" path2 \"%s\"\n",
	       hostname, rank, num_procs, path1, path2);

	if (rank == 0)
		which = path1;
	else
		which = path2;
	test_1node_1file(which);

	do_barrier();

	test_2nodes_1file(path1, rank == 0);

	do_barrier();

	test_deadlocks(path1, path2, rank == 0);

        MPI_Finalize();
        return 0;
}
