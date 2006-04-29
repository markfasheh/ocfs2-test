
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define __u32 	unsigned long



int main(int argc, char **argv)
{
	int wait;
	int64_t bytes;
	int fd, ret;
	char *fname;
	char buf[10];
	int64_t off;
	struct timespec ts,rem;
	int loops = 1;
//	char tmpbuf[100];

	if (argc < 5) {
		printf("usage: %s fname KB wait(in ms) loops\n", argv[0]);
		exit(1);
	}

	fname = argv[1];
	bytes =  1024 * atoi(argv[2]);
	wait = atoi(argv[3]);
	loops = atoi(argv[4]);

	fd = open64(fname, O_RDWR|O_CREAT|O_LARGEFILE, 0666);
	if (fd == -1) {
		printf("open failed.\n");
		printf("error %d: %s\n", errno, strerror(errno));
		exit(1);
	}

	off = lseek64(fd, 0, SEEK_END);
	if (off == -1)
	{
		printf("lseek failed to seek to 0!\n");
		printf("error %d: %s\n", errno, strerror(errno));
		exit(1);
	}
	printf("seek to end at offset %llu\n", off);

	strcpy(buf, "123456789");
	ts.tv_sec = (wait/1000);
	ts.tv_nsec = (wait%1000)*(1000*1000);

	while (1) {
		off = lseek64(fd, bytes, SEEK_END);
		if (off == -1) {
			printf("lseek failed!\n");
			printf("error %d: %s\n", errno, strerror(errno));
			exit(1);
		}

		ret = write(fd, buf, 10);
		if (ret == -1) {
			printf("write failed!\n");
			printf("error %d: %s\n", errno, strerror(errno));
			exit(1);
		}

		printf("write succeeded at offset %lld...  sleeping %d ms...\n",
		       off, wait);

		ret = nanosleep(&ts, &rem);
		if (ret == -1) {
			printf("nanosleep failed!\n");
			printf("error %d: %s\n", errno, strerror(errno));
			exit(1);
		}

		close(fd);

		if (--loops <= 0)
			break;
#if 0
		if (--loops <= 0) {
			printf("How many more loops ? ");
			gets(tmpbuf);
			loops = atoi(tmpbuf);
			printf("%d\n", loops);
		}
#endif

		fd = open(fname, O_RDWR|O_LARGEFILE);
		if (fd == -1) {
			printf("open failed.\n");
			printf("error %d: %s\n", errno, strerror(errno));
			exit(1);
		}
	}	
	close(fd);

	return 1;
}
