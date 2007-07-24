#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <aio.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>

#include <fcntl.h>

#include <libaio.h>

#define BUFSIZE (128 * 1024)

#define align_pow2(ptr, val) ( ptr + val - ((unsigned long)ptr & (val - 1)) )

int main(int argc, char **argv)
{
	io_context_t ctx;
	struct iocb iocb;
	struct iocb *iocbs[1];
	struct io_event event;
	struct stat st;
	char *ptr, *buf, one[PATH_MAX], two[PATH_MAX];
	size_t buflen, ptrlen;
	long rc;
	int fd1, fd2;

	if ((argc < 2) || (strcmp(argv[1],"-h") == 0)) {
		printf("Usage: partial_aio_direct <filename>\n");
		exit(1);
	}

	snprintf(one, sizeof(one), "%s-1", argv[1]);
	snprintf(two, sizeof(two), "%s-2", argv[1]);

	unlink(one);
	unlink(two);

	fd1 = open(one, O_CREAT|O_RDWR|O_DIRECT, 0644);
	if (fd1 < 0) {
		perror("open");
		exit(1);
	}

	fd2 = open(two, O_CREAT|O_RDWR|O_DIRECT, 0644);
	if (fd2 < 0) {
		perror("open");
		exit(1);
	}

	if (fstat(fd1, &st)) {
		perror("open");
		exit(1);
	}

	if (ftruncate(fd1, st.st_blksize)) {
		perror("ftruncate()");
		exit(1);
	}
	if (ftruncate(fd2, st.st_blksize)) {
		perror("ftruncate()");
		exit(1);
	}
	if (ftruncate(fd1, st.st_blksize * 2)) {
		perror("ftruncate()");
		exit(1);
	}
	if (ftruncate(fd2, st.st_blksize * 2)) {
		perror("ftruncate()");
		exit(1);
	}

	/* assumes this is a power of two */
	buflen = st.st_blksize * 2;
	ptrlen = st.st_blksize * 4;

	/* some slop */
	ptr = calloc(1, ptrlen);
	if (ptr == NULL) {
		perror("calloc");
		exit(1);
	}

	/* align buf to the next natural buflen alignment after ptr */
	buf = align_pow2(ptr, st.st_blksize);

	rc = io_queue_init(100, &ctx);
	if (rc) {
		printf("queue_init: %ld\n", rc);
		exit(1);
	}

	io_prep_pwrite(&iocb, fd1, buf, buflen, 0);

	memset(ptr, 0x42, ptrlen);

	printf("saw block size %lu, writing with buflen %lu\n", st.st_blksize,
		buflen);

	iocbs[0] = &iocb;

	rc = io_submit(ctx, 1, iocbs);
	if (rc != 1) {
		printf("submit: %ld\n", rc);
		exit(1);
	}

	rc = io_getevents(ctx, 1, 1, &event, NULL);
	printf("got %ld: data %p iocb %p res %ld res2 %ld\n", rc,
			event.data, event.obj, event.res, event.res2);

	return 0;
}
