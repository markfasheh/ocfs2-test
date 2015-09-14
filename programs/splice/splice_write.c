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
	long int slen = 0;
	long int to_write = 1000000;

	if (argc < 2) {
		printf("Usage: ls | ./splice_write out\n");
		exit(-1);
	}
	fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		printf("open file failed.\n");
		exit(-1);
	}
	if (argc == 3)
		to_write = atol(argv[2]);
	while (to_write > 0) {
		slen = splice(STDIN_FILENO, NULL, fd, NULL, to_write, 0);
		if (slen < 0)
			fprintf(stderr, "splice failed.\n");
		else if (slen == 0)
			break;
		else {
			to_write -= slen;
			fprintf(stderr, "spliced length = %ld\n",slen);
		}
	}
	close(fd);
	if (slen < 0)
		exit(-1);
	return 0;
}

