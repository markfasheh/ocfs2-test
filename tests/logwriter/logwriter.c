#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <stdlib.h>

#define DEFAULT_SLEEP 1000000
#define HOSTNAME_SZ 100
#define BUFSZ (80 * 4)
#define TIMESZ 30

#define OPEN_FLAGS (O_CREAT|O_WRONLY|O_APPEND|O_SYNC)

#define LOG_FORMAT   " %s"
#define LOG_ARGS     "This is a log entry."

#define OPEN_ONCE 0

#define PRINTERR(err)                                                         \
        printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",          \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

int main(int argc, char **argv)
{
	unsigned int usec = DEFAULT_SLEEP;
	char *logfile;
	int fd, len, written;
	int status = 0;
	char hostname[HOSTNAME_SZ];
	char buffer[BUFSZ];
	char timebuf[TIMESZ];
	time_t systime;

	if ((argc < 2) || (argc > 3)) {
		printf("%s logfile [sleeptime]\n", argv[0]);
		printf("will write out a log to logfile, sleeping \n"
		       "\"sleeptime\" microseconds between writes.\n"
		       "\"sleeptime\" defaults to 1000000.\n");
		return(0);
	}

	logfile = argv[1];

	if (argc == 3)
		usec = atoi(argv[2]);

	printf("write to file %s and sleep %u microseconds\n", logfile, usec);

	if (gethostname(hostname, HOSTNAME_SZ) == -1) {
                status = errno;
                PRINTERR(status);
                goto bail;
        }

#ifdef OPEN_ONCE
	fd = open(logfile, OPEN_FLAGS, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		status = errno;
		PRINTERR(status);
		goto bail;
	}
#endif

	while(1) {
		systime = time(NULL);
		if (systime == ((time_t)-1)) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}

		memset(timebuf, 0, TIMESZ);
		if (ctime_r(&systime, timebuf) == NULL) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}

		len = strlen(timebuf);
		if (len == 0) {
			printf("wtf, mate?\n");
			goto bail;
		}
		timebuf[len-1] = '\0';

		len = snprintf(buffer, BUFSZ, "%s %s %s:"LOG_FORMAT"\n", 
			       hostname, timebuf, argv[0], LOG_ARGS);

#ifndef OPEN_ONCE
		fd = open(logfile, OPEN_FLAGS,
			  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if (fd == -1) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
#endif

		written = write(fd, buffer, len);
		if (written == -1) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
		if (written < len)
			printf("Short write, %d!\n", written);

#ifndef OPEN_ONCE
		close(fd);
#endif
		printf("%s", buffer);
		if (usleep(usec)) {
			status = errno;
			PRINTERR(status);
			goto bail;
		}
	}

#ifdef OPEN_ONCE
	close(fd);
#endif

bail:
	return status;
}
