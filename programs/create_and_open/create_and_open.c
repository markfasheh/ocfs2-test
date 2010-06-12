/*
 * create_and_open.c
 *
 * entry point for fswrk
 *
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
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>

#define NUMFILES    3

int create_files(int writefd);
int open_files(int readfd);

char *workplace;

int main(int argc, char **argv)
{
	int status = 0;
	int comm[2];
	int pid, readfd, writefd;

	if ((argc < 2) || (strcmp(argv[1], "-h") == 0)) {
		fprintf(stderr, "Usage: create_and_open <workdir>\n");
		return 1;
	}

	workplace = argv[1];

	status = pipe(comm);
	if (status < 0) {
		printf("Could not pipe, %d\n", status);
		goto bail;
	}

	readfd = comm[0];
	writefd = comm[1];

	pid = fork();
	if (pid == -1) {
		printf("Could not fork!, %d\n", status);
		goto bail;
	}

	if (pid)
		status = create_files(writefd);
	else
		status = open_files(readfd);

bail:
	return(status);
}

#define NMMX 256
int create_files(int writefd)
{
	char buff[NMMX];
	int len, i, status;

	for(i = 0; i < NUMFILES; i++) {
		memset(buff, 0, NMMX);
		snprintf(buff, NMMX-1, "%s/testfile%d", workplace, i);

		status = mknod(buff, S_IFREG|S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
			       0);
		if (status < 0) {
			printf("Could not mknod, \"%s\", %d\n", buff, status);
			goto bail;
		}

		len = strlen(buff);
		status = write(writefd, &len, sizeof(len));
		if (status < 0) {
			printf("Could not write len to pipe, %d\n", status);
			goto bail;
		}

		status = write(writefd, buff, len);
		if (status < 0) {
			printf("Could not write buff to pipe, %d\n", status);
			goto bail;
		}
	}

	status = 0;
bail:
	return(status);
}

int open_files(int readfd)
{
	char buff[256];
	int len, i, status;
	int tmpfd;

	for(i = 0; i < NUMFILES; i++) {
		memset(buff, 0, NMMX);

		status = read(readfd, &len, sizeof(len));
		if (status < 0) {
			printf("Could not read len from pipe, %d\n", status);
			goto bail;
		}

		status = read(readfd, buff, len);
		if (status < 0) {
			printf("Could not read buff from pipe, %d\n", status);
			goto bail;
		}

		tmpfd = open(buff, O_RDWR);
		if (tmpfd < 0) {
			status = tmpfd;
			printf("Could not open file \"%s\", %d", buff, tmpfd);
			goto bail;
		}
	}

bail:
	return(status);
}
