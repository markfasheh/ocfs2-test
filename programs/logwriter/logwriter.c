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
 * Description: This test will open a file and write to it in specific 
 * 		intervals. It takes two arguments, interval and number of
 * 		writes it will perform.
 *
 *              This test really has no cluster relevance if running in 
 *              stand-alone mode. Needs a script to coordinate cluster test.
 *
 * Author     : Mark Fasheh
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

#define DEFAULT_SLEEP 1000000
#define DEFAULT_COUNT 1000000
#define HOSTNAME_SZ 100
#define BUFSZ (80 * 4)
#define TIMESZ 30

#define OPEN_FLAGS (O_CREAT|O_WRONLY|O_APPEND|O_SYNC)

#define LOG_FORMAT   " %s"
#define LOG_ARGS     "This is a log entry."

#define OPEN_ONCE 0

#define PRINTERR(err)                                                         \
        printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",          \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

int main(int argc, char **argv)
{
	unsigned int usec = DEFAULT_SLEEP;
	unsigned int loopc = DEFAULT_COUNT;
	char *logfile;
	int fd, len, written, count;
	int status = 0;
	char hostname[HOSTNAME_SZ];
	char buffer[BUFSZ];
	char timebuf[TIMESZ];
	time_t systime;

	if ((argc < 2) || (argc > 4)) {
           printf("Usage: %s logfile [sleeptime] [loop count]\n", argv[0]);
	   printf("will write out a log to logfile, sleeping \n"
	          "\"sleeptime\" microseconds between writes.\n"
		  "\"loop count\" Numer of time it will write to logfile.\n"
		  "\"sleeptime\" defaults to 1000000.\n"
		  "\"loop count\" defaults to 1000000.\n");
           return(0);
	}

	logfile = argv[1];

	if (argc >= 3)
		usec = atoi(argv[2]);

	if (argc >= 4)
		loopc = atoi(argv[3]);

	printf("write %u times to file %s and sleep %u microseconds\n", \
		loopc, \
		logfile, \
		usec);

	if (gethostname(hostname, HOSTNAME_SZ) == -1) {
                status = errno;
                PRINTERR(status);
                goto bail;
        }

#ifdef OPEN_ONCE
	fd = open(logfile, OPEN_FLAGS, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		status = errno;
		PRINTERR(status);
		goto bail;
	}
#endif

	for (count=0; count<loopc; count++) {
		systime = time(NULL);
		if (systime == ((time_t)-1)) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}

		memset(timebuf, 0, TIMESZ);
		if (ctime_r(&systime, timebuf) == NULL) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}

		len = strlen(timebuf);
		if (len == 0) {
			printf("wtf, mate?\n");
			goto bail;
		}
		timebuf[len-1] = '\0';

		len = snprintf(buffer, BUFSZ, "%s %s %s:"LOG_FORMAT"\n", 
			       hostname, timebuf, argv[0], LOG_ARGS);

#ifndef OPEN_ONCE
		fd = open(logfile, OPEN_FLAGS,
			  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if (fd == -1) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
#endif

		written = write(fd, buffer, len);
		if (written == -1) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
		if (written < len)
			printf("Short write, %d!\n", written);

#ifndef OPEN_ONCE
		close(fd);
#endif
		printf("%s", buffer);
		if (usleep(usec)) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
	}

#ifdef OPEN_ONCE
	close(fd);
#endif

bail:
	return status;
}
