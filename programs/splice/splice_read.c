/* splice_read.c */
#include "splice_test.h"

int main(int argc, char *argv[])
{
	int fd;
	int slen;
	
	if (argc < 2) {
		printf("Usage: ./splice_read out | cat\n");
		exit(-1);
	}

	fd = open(argv[1], O_RDONLY, 0644);
	if (fd == -1) {
		printf("open file failed.\n");
		exit(-1);
	}
	slen = splice(fd, NULL, STDOUT_FILENO, NULL, 10000000, 0);
	if (slen < 0)
		fprintf(stderr, "splice failed.\n");
	else
		fprintf(stderr, "spliced length = %d\n",slen);
	close(fd);
	if (slen <0)
		exit(-1);
	return 0;
}

