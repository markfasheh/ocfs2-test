#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>

#define NUMFILES    3

int create_files(int writefd);
int open_files(int readfd);

int main(int argc, char **argv)
{
	int status = 0;
	int comm[2];
	int pid, readfd, writefd;


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
		snprintf(buff, NMMX-1, "testfile%d", i);

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
//		printf("created \"%s\"\n", buff);
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
//		printf("opened \"%s\"\n", buff);
	}

bail:
	return(status);
}
