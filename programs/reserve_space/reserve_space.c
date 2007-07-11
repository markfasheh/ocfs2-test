#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <inttypes.h>

#include "reservations.h"

int main(int argc, char ** argv) {
	int fd, ret;
	char *filename;
	struct ocfs2_space_resv sr;
	int cmd;

	if (argc < 6) {
usage:
		printf("Usage: %s <path> <resv | unresv> <whence> <start> <size>\n", argv[0]);
		printf("  start and size are expected to be in bytes\n");
		printf("  whence 0 == SEEK_SET, 1 == SEEK_CUR, 2 == SEEK_END\n");
		return 1;
	}

	memset(&sr, 0, sizeof(sr));

	filename = argv[1];
	if (strcasecmp(argv[2], "resv") == 0)
		cmd = OCFS2_IOC_RESVSP64;
	else if (strcasecmp(argv[2], "unresv") == 0)
		cmd = OCFS2_IOC_UNRESVSP64;
	else
		goto usage;
	sr.l_whence = atoi(argv[3]);
	sr.l_start = atoll(argv[4]);
	sr.l_len = atoll(argv[5]);

	printf("File: %s\n", filename);
	printf("cmd = %s\n", argv[1]);
	printf("l_whence = %d\n", sr.l_whence);
	printf("l_start = %lld\n", sr.l_start);
	printf("l_len = %lld\n", sr.l_len);

	fd = open(filename, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "Error %d from open: \"%s\"\n", ret,
			strerror(ret));
		goto out;
	}

	ret = ioctl(fd, cmd, &sr);
	if (ret == -1) {
		ret = errno;
		fprintf(stderr, "Error %d from ioctl: \"%s\"\n", ret,
			strerror(ret));
	}

out:
	close(fd);

	return 0;
}
