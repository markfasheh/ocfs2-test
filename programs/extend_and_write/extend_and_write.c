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
 * Description: This test just creates a file and have it
 * 		extended by the child process.
 *
 *              This test has no cluster relevance.
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
#include <sys/wait.h>
#include <stdlib.h>

#define DO_FSYNC

#define PROGNAME "extend_and_write"
#define PRINTERR(err)                                                         \
        printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",	      \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

#define SZ 1024
#define NUMWRITES 10240

int count;

static void Usage()
{
     printf("Usage: %s -h -f <filename> [-s <extend size>]" 
	    " [-n <numwrites>]\n", PROGNAME);
     printf("will create and extend a file, n times.\n\n"
            "<extend size> Size of the extend (Default=1024)\n"
            "<numwrites> Number of writes to be performed (Default=10240).\n");
     exit(1);
}
int main(int argc, char **argv)
{
	int status = 0;
	int fd;
	char buffer[SZ];
	char filename[256] = "/tmp/extend_and_write.txt";
	int off_t = SZ;
	int loops = NUMWRITES;
	pid_t pid;
//	off_t len = SZ;
	int i, c;

	opterr = 0;
	count = argc;
	while ((c = getopt(argc, argv, ":h:f:s:n:H:-:")) != EOF)
	{
		switch (c)
		{
		   case 'h':
			Usage();
			break;
		   case 'H':
			Usage();
			break;
	   	   case '-':
			if (!strcmp(optarg, "help"))
			   Usage();
			else
			{
			   fprintf(stderr, "%s: Invalid option: \'--%s\'\n",
			   PROGNAME, optarg);
		           return -EINVAL;
			}
                   case 'f':
			snprintf(filename,255,"%s",optarg);
                        if ((*filename == '-')) {
                           fprintf(stderr, "%s: Invalid file name: %s\n",
                                   PROGNAME, optarg);
                           return -EINVAL;
                        }
                        break;
                   case 'n':
                        loops = atoi(optarg);
                        if (!loops) {
                           fprintf(stderr, "%s: Invalid numwrites: %s\n",
                                   PROGNAME, optarg);
                           return -EINVAL;
                        }
                        break;
                   case 's':
                        off_t = atoi(optarg);
                        if (!off_t) {
                           fprintf(stderr, "%s: Invalid extend size: %s\n",
                                   PROGNAME, optarg);
                           return -EINVAL;
                        }
                        break;
		   default:
			Usage();
			break;
		}
	}

	printf("file will be %d bytes after this run\n", 2 * off_t * loops);
	fd = open(filename, O_CREAT|O_TRUNC|O_RDWR|O_APPEND, 
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		status = errno;
		PRINTERR(status);
		goto bail;
	}

	pid = fork();
	/* ok, the parent will do the writing, the child the extending */
	/* actually, maybe at first they'll both write */
	if (pid) {
		memset(buffer, 'A', off_t);
		for (i = 0; i < loops; i++) {
			status = write(fd, buffer, off_t);
			if (status < 0) {
				PRINTERR(status);
				goto bail;
			}
			if (status != off_t) {
				printf("(%d) Short write! size = %d, i=%d\n",
				       getpid(), status, i);
				status = 0;
				goto bail;
			}
//			usleep(15);
#ifdef DO_FSYNC
			fsync(fd);
#endif
		}
		waitpid(pid, NULL, 0);
		printf("parent is exiting.\n");
	} else {
		memset(buffer, 'B', off_t);
		for (i = 0; i < loops; i++) {
			status = write(fd, buffer, off_t);
			if (status < 0) {
				PRINTERR(status);
				goto bail;
			}
			if (status != off_t) {
				printf("(%d) Short write! size = %d, i=%d\n",
				       getpid(), status, i);
				status = 0;
				goto bail;
			}
//			usleep(15);
#ifdef DO_FSYNC
			fsync(fd);
#endif
		}
		printf("child is exiting.\n");
	}

bail:
	return(status);
}
