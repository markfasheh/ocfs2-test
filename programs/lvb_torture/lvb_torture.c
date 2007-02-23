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
 */
/*
 * My first MPI program. Ripped from write_append_truncate.c
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <linux/types.h>

#include "mpi.h"

#include <o2dlm/o2dlm.h>

#include <ocfs2/byteorder.h>

#define DEFAULT_ITER     10000

#define HOSTNAME_SIZE 50
static char hostname[HOSTNAME_SIZE];
static char *prog;
static int rank, num_procs;
static unsigned long long max_iter = DEFAULT_ITER;
static sig_atomic_t caught_sig = 0;

static void handler(int signum)
{
    caught_sig = signum;
}

static int setup_signals()
{
    int rc = 0;
    struct sigaction act;
    memset(&act, 0, sizeof(act));

    act.sa_sigaction = NULL;
    sigemptyset(&act.sa_mask);
    act.sa_handler = handler;
#ifdef SA_INTERRUPT
    act.sa_flags = SA_INTERRUPT;
#endif

    rc += sigaction(SIGHUP, &act, NULL);
    rc += sigaction(SIGTERM, &act, NULL);
    rc += sigaction(SIGINT, &act, NULL);
    act.sa_handler = SIG_IGN;
    rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */
    
    return rc;
}

/* Print process rank, loop count, message, and exit (i.e. a fatal error) */
static void rprintf(int rank, const char *fmt, ...)
{
	va_list       ap;

	printf("rank %d: ", rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void run_test(struct o2dlm_ctxt *dlm, char *lockid)
{
	unsigned long long iter = 0;
	unsigned long long expected, to_write = 0;
	int ret;
	unsigned int read, written;
	errcode_t err;
	enum o2dlm_lock_level level;
	__u64 lvb;

	while (iter < max_iter && !caught_sig) {
		expected = iter;

		if ((iter % num_procs) == rank)
			level = O2DLM_LEVEL_EXMODE;
		else
			level = O2DLM_LEVEL_PRMODE;

		if (level == O2DLM_LEVEL_PRMODE) {
			ret = MPI_Barrier(MPI_COMM_WORLD);
			if (ret != MPI_SUCCESS)
				rprintf(rank, "read MPI_Barrier failed: %d\n", ret);
			err = o2dlm_lock(dlm, lockid, 0, level);
			if (err)
				rprintf(rank, "o2dlm_lock failed: %d\n", err);

			expected++;
		} else {
			err = o2dlm_lock(dlm, lockid, 0, level);
			if (err)
				rprintf(rank, "o2dlm_lock failed: %d\n", err);

			ret = MPI_Barrier(MPI_COMM_WORLD);
			if (ret != MPI_SUCCESS)
				rprintf(rank, "read MPI_Barrier failed: %d\n", ret);
			to_write = iter + 1;
		}

		err = o2dlm_read_lvb(dlm, lockid, (char *)&lvb, sizeof(lvb),
				     &read);
		if (err)
			rprintf(rank, "o2dlm_read_lvb failed: %d\n", err);

		lvb = be64_to_cpu(lvb);

		if (level == O2DLM_LEVEL_PRMODE)
			printf("%s: read  iter: %llu, lvb: %llu exp: %llu\n",
			       hostname, (unsigned long long)iter,
			       (unsigned long long)lvb,
			       (unsigned long long)expected);
		else
			printf("%s: write iter: %llu, lvb: %llu wri: %llu\n",
			       hostname, (unsigned long long)iter,
			       (unsigned long long)lvb,
			       (unsigned long long)to_write);

		fflush(stdout);

		if (lvb != expected) {
			printf("Test failed! %s: rank %d, read lvb %llu, expected %llu\n",
			       hostname, rank, (unsigned long long) lvb,
			       (unsigned long long) expected);
			MPI_Abort(MPI_COMM_WORLD, 1);
		}

		if (level == O2DLM_LEVEL_EXMODE) {
			lvb = cpu_to_be64(to_write);

			err = o2dlm_write_lvb(dlm, lockid, (char *)&lvb,
					      sizeof(lvb), &written);
			if (err)
				rprintf(rank, "o2dlm_write_lvb failed: %d\n", err);
			if (written != sizeof(lvb))
				rprintf(rank, "o2dlm_write_lvb() wrote %d, we asked for %d\n", written, sizeof(lvb));
		}

		err = o2dlm_unlock(dlm, lockid);
		if (err)
			rprintf(rank, "o2dlm_unlock failed: %d\n", err);

		/* This second barrier is not necessary and can be
		 * commented out to ramp the test up */
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			rprintf(rank, "unlock MPI_Barrier failed: %d\n", ret);

		iter++;
	}
}

static void clear_lock(struct o2dlm_ctxt *dlm, char *lockid)
{
	char empty[O2DLM_LOCK_ID_MAX_LEN];
	unsigned int written;
	errcode_t err;

	memset(empty, 0, O2DLM_LOCK_ID_MAX_LEN);

	err = o2dlm_lock(dlm, lockid, 0, O2DLM_LEVEL_EXMODE);
	if (err)
		rprintf(rank, "o2dlm_lock failed: %d\n", err);

	err = o2dlm_write_lvb(dlm, lockid, empty, O2DLM_LOCK_ID_MAX_LEN,
			      &written);
	if (written != O2DLM_LOCK_ID_MAX_LEN)
		rprintf(rank, "o2dlm_write_lvb() couldn't clear lockres\n");

	err = o2dlm_unlock(dlm, lockid);
	if (err)
		rprintf(rank, "o2dlm_unlock failed: %d\n", err);

	printf("%s: cleared lock %s for use\n", hostname, lockid);
}

#define HB_CTL_PATH "/sbin/ocfs2_hb_ctl"

/* hb_ctl code stolen from mount.ocfs2.c */
static int check_for_hb_ctl(const char *hb_ctl_path)
{
	int ret;

	ret = access(hb_ctl_path, X_OK);
	if (ret < 0) {
		ret = errno;
		return ret;
	}

	return ret;
}

static int run_hb_ctl(const char *hb_ctl_path,
		      const char *device, const char *arg)
{
	int ret = 0;
	int child_status;
	char * argv[5];
	pid_t child;

	child = fork();
	if (child < 0) {
		ret = errno;
		goto bail;
	}

	if (!child) {
		argv[0] = (char *) hb_ctl_path;
		argv[1] = (char *) arg;
		argv[2] = "-d";
		argv[3] = (char *) device;
		argv[4] = NULL;

		ret = execv(argv[0], argv);

		ret = errno;
		exit(ret);
	} else {
		ret = waitpid(child, &child_status, 0);
		if (ret < 0) {
			ret = errno;
			goto bail;
		}

		ret = WEXITSTATUS(child_status);
	}

bail:
	return ret;
}

static int start_heartbeat(const char *hb_ctl_path,
			   const char *device)
{
	int ret;

	ret = check_for_hb_ctl(hb_ctl_path);
	if (ret)
		return ret;

	ret = run_hb_ctl(hb_ctl_path, device, "-S");

	return ret;
}

static int stop_heartbeat(const char *hb_ctl_path,
			  const char *device)
{
	return run_hb_ctl(hb_ctl_path, device, "-K");
}

static char *dlmfs_path = "/dlm/";
static char *domain = NULL;
static char *lockid = NULL;
static char *hb_dev = NULL;

static void usage(char *prog)
{
	printf("usage: %s [-h <heartbeat device>] [-d <dlmfs path>] [-i <iterations>] <domain> <lockname>\n", prog);
	printf("<iterations> defaults to %d\n", DEFAULT_ITER);
	printf("<dlmfs path> defaults to %s\n", dlmfs_path);
	printf("if <heartbeat device> is given, heartbeat will be started for "
	       "you, otherwise it is expected to be up.\n");

	MPI_Finalize();
	exit(1);
}

int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "h:d:i:");
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			hb_dev = strdup(optarg);
			break;
		case 'd':
			dlmfs_path = strdup(optarg);
			break;
		case 'i':
			max_iter = atoll(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 2)
		return EINVAL;

	domain = argv[optind];
	lockid = argv[optind+1];

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;
	errcode_t error;
	struct o2dlm_ctxt *dlm = NULL;

	initialize_o2dl_error_table();

	error = MPI_Init(&argc, &argv);
	if (error != MPI_SUCCESS)
		rprintf(-1, "MPI_Init failed: %d\n", error);

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage(prog);

	error = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        if (error != MPI_SUCCESS)
                rprintf(-1, "MPI_Comm_rank failed: %d\n", error);

	error = MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
	if (error != MPI_SUCCESS)
		rprintf(rank, "MPI_Comm_size failed: %d\n", error);

        if (gethostname(hostname, HOSTNAME_SIZE) < 0)
                rprintf(rank, "gethostname failed: %s\n", strerror(errno));

        printf("%s: rank: %d, nodes: %d, dlm: %s, dom: %s, lock: %s, iter: %llu\n", hostname, rank, num_procs, dlmfs_path, domain, lockid, (unsigned long long) max_iter);

	error = setup_signals();
	if (error)
		rprintf(rank, "setup_signals failed\n");

	if (hb_dev) {
		ret = start_heartbeat(HB_CTL_PATH, hb_dev);
		if (ret)
			rprintf(rank, "start_heartbeat failed\n");
	}

	error = o2dlm_initialize(dlmfs_path, domain, &dlm);
	if (error)
		rprintf(rank, "o2dlm_initialize failed: %d\n", error);

	if (rank == 0)
		clear_lock(dlm, lockid);

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		rprintf(rank, "prep MPI_Barrier failed: %d\n", ret);

	run_test(dlm, lockid);

	error = o2dlm_destroy(dlm);
	if (error)
		rprintf(rank, "o2dlm_destroy failed: %d\n", error);

	if (hb_dev) {
		ret = stop_heartbeat(HB_CTL_PATH, hb_dev);
		if (ret)
			rprintf(rank, "stop_heartbeat failed\n");
	}

        MPI_Finalize();
        return 0;
}
