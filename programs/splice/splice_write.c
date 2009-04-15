/* splice_write.c */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

int main(int argc, char *argv[])
{
	int fd;
	int slen;

	if (argc < 2) {
		printf("Usage: ls | ./splice_write out\n");
		exit(-1);
	}
	fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		printf("open file failed.\n");
		exit(-1);
	}
	slen = splice(STDIN_FILENO, NULL, fd, NULL, 10000000, 0);
	if (slen < 0)
		fprintf(stderr, "splice failed.\n");
	else
		fprintf(stderr, "spliced length = %d\n",slen);
	close(fd);
	if (slen < 0)
		exit(-1);
	return 0;
}

