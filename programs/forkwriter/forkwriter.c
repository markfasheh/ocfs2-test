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
/*
 * Program:	forkwriter.c
 *
 * Description: This test launches forks P processes each of which create
 * 		a file with the same name and issue few writes of few bytes each
 * 		before closing the file and exiting. The parent process meanwhile
 * 		waits for the childs to all die before launching the next set
 * 		of P processes. The parent process loops L times.
 *
 *              This test really has no cluster relevance if running in 
 *              stand-alone mode. Needs a script to coordinate cluster test.
 *
 * Author     : Sunil Mushran
 * 
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define DEFAULT_SLEEP 50000
#define DEFAULT_LOOPS 100
#define DEFAULT_PROCS 2

#define NUM_WRITES 100

#define HOSTNAME_SZ 100
#define BUFSZ (80 * 4)
#define TIMESZ 30

#define OPEN_FLAGS (O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE)

#define LOG_FORMAT   " %s"
#define LOG_ARGS     "This is a log entry for pid "

#define PRINTERR(err)                                                         \
        fprintf(stderr, "[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",          \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

static int do_child(char *logfile, int loop, int proc, unsigned long usec)
{
	char *filename = NULL;
	int ret = -1;
	int fd = -1;
	int len, written;
	char hostname[HOSTNAME_SZ];
	char buffer[BUFSZ];
	char timebuf[TIMESZ];
	time_t systime;
	int i;

	asprintf(&filename, "%s_%03d", logfile, loop);

	printf("Pid %d, Loop %d, Process %d, File %s\n", getpid(), loop, proc,
	       filename);

	if (gethostname(hostname, HOSTNAME_SZ) == -1) {
                PRINTERR(errno);
                goto bail;
        }

	systime = time(NULL);
	if (systime == ((time_t)-1)) {
		PRINTERR(errno);
		goto bail;
	}

	memset(timebuf, 0, TIMESZ);
	if (ctime_r(&systime, timebuf) == NULL) {
		PRINTERR(errno);
		goto bail;
	}

	len = strlen(timebuf);
	if (len == 0) {
		fprintf(stderr, "what's up, mate?\n");
		goto bail;
	}
	timebuf[len-1] = '\0';

	len = snprintf(buffer, BUFSZ, "%s %s:"LOG_FORMAT" %d.\n", 
		       hostname, timebuf, LOG_ARGS, getpid());

	fd = open(filename, OPEN_FLAGS, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		PRINTERR(errno);
		goto bail;
	}

	for (i = 0; i < NUM_WRITES; i++) {
		written = write(fd, buffer, len);
		if (written == -1) {
			PRINTERR(errno);
			goto bail;
		}
		if (written < len)
			fprintf(stderr, "Short write, %d!\n", written);

		if (usleep(usec)) {
			PRINTERR(errno);
			goto bail;
		}
	}

	ret = 0;

bail:
	if (fd > -1)
		close(fd);
	if (filename)
		free(filename);
	exit(ret);
}

int main(int argc, char **argv)
{
	unsigned long usecs = DEFAULT_SLEEP;
	int loops = DEFAULT_LOOPS;
	int procs = DEFAULT_PROCS;
	char *logfile;
	int status = 0;
	int i, j;
	pid_t pid;
	pid_t *pids = NULL;

	if ((argc < 2) || (argc > 5)) {
		printf("usage: %s file [loops] [procs] [sleep]\n\n",
		       basename(argv[0]));
		printf("\tfile:\tname used to generate the logfile created\n"
		       "\tloops:\tnumber of times the processes will be forked (%d)\n"
		       "\tprocs:\tnumber of processes forked in each loop (%d)\n"
		       "\tsleep:\tms in between each write (%d)\n",
		       DEFAULT_LOOPS, DEFAULT_PROCS, DEFAULT_SLEEP);
		return(0);
	}

	logfile = argv[1];

	if (argc >= 3)
		loops = atoi(argv[2]);

	if (argc >= 4)
		procs = atoi(argv[3]);

	if (argc >= 5)
		usecs = atoi(argv[4]);

	pids = malloc(procs * sizeof(pid_t));
	if (!pids) {
		fprintf(stderr, "ENOMEM\n");
		goto bail;
	}

	for (i = 0; i < loops; ++i) {
		memset(pids, 0, (procs * sizeof(pid_t)));

		for (j = 0; j < procs; ++j) {
			pid = fork();
			if (pid == -1) {
                		PRINTERR(errno);
				goto bail;
			}

			if (!pid)
				do_child(logfile, i, j, usecs);
			else
				pids[j] = pid;
		}

		for (j = 0; j < procs; ++j) {
			printf("(%d) waiting for %d\n", getpid(), pids[j]);
			waitpid(pids[j], &status, 0);
		}
	}

bail:
	return 0;
}
