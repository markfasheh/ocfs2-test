#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#define DO_FSYNC

#define PRINTERR(err)                                                         \
        printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",	      \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

#define SZ 1024
#define NUMWRITES 10240

int main(int argc, char **argv)
{
	int status = 0;
	int fd;
	char buffer[SZ];
	pid_t pid;
//	off_t len = SZ;
	int i;

	printf("file will be %d bytes after this run\n", 2 * SZ * NUMWRITES);
	fd = open("testfile", O_CREAT|O_TRUNC|O_RDWR|O_APPEND, 
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
		memset(buffer, 'A', SZ);
		for (i = 0; i < NUMWRITES; i++) {
			status = write(fd, buffer, SZ);
			if (status < 0) {
				PRINTERR(status);
				goto bail;
			}
			if (status != SZ) {
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
		memset(buffer, 'B', SZ);
		for (i = 0; i < NUMWRITES; i++) {
			status = write(fd, buffer, SZ);
			if (status < 0) {
				PRINTERR(status);
				goto bail;
			}
			if (status != SZ) {
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
