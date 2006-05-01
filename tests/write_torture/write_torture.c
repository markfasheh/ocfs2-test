#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

//#define dprintf printf
#define dprintf(str, ...) 

static pid_t mypid, parent;
static int die = 0;
static int fd;
#define BLKLEN 8102
static char block[BLKLEN];

static int logfd = STDOUT_FILENO;

static void __logprint(const char *fmt, ...)
{
	int len;
	static char str[4096];
	va_list ap;

	va_start(ap, fmt);

	len = vsnprintf(str, 4096, fmt, ap);
	if (len == -1) {
		len = errno;
		fprintf(stderr, "Can't log, error %d\n", len);
		return;
	}

	write(logfd, str, len);
}

#define logprint(fmt, args...) __logprint("[%u]: "fmt, mypid, args)

static unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + ((rand() % max) - min);
}

static void random_sleep(unsigned int max_usecs)
{
	int r = rand();
	unsigned int usec;

	usec = r % max_usecs;

	dprintf("%u: sleep %u usec\n", mypid, usec);
	usleep(usec);
}

static void signal_handler(int sig)
{
	dprintf("%u: signal %d recieved\n", mypid, sig);
	die = 1;
}

static int launch_child(char *fname, int open_flags, int (*newmain)(void))
{
	pid_t pid;
	int ret = 0;

	pid = fork();
	if (!pid) {
		fd = open(fname, open_flags);
		if (fd == -1) {
			ret = errno;
			fprintf(stderr,
				"Error %d opening \"%s\"\n", ret, fname);
			exit(ret);
		}

		mypid = getpid();
		srand(mypid);
		ret = newmain();
		exit(ret);
	}
	if (pid == -1) {
		ret = errno;
		fprintf(stderr, "could not fork: %d\n", ret);
	}

	return ret;
}

static int do_write(int fd, const char *buf, unsigned int len)
{
	int written, ret = 0;

	written = write(fd, buf, len);
	if (written == -1) {
		ret = errno;
		fprintf(stderr, "%d: append write failure %d\n", mypid,
			ret);
	} else if (written < len)
		fprintf(stderr, "%d: short write!\n", mypid);

	return ret;
}

static int append_writer(void)
{
	int ret = 0;
	int len;

	memset(block, 'a', BLKLEN);

	while (!die) {
		len = get_rand(1, BLKLEN + 1);
		logprint("append write len\t\t: %d\n", len);

		ret = do_write(fd, block, len);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

static int get_i_size(int fd, unsigned long *size)
{
	struct stat stat;
	int ret;

	ret = fstat(fd, &stat);
	if (ret == -1) {
		ret = errno;
		fprintf(stderr, "%d: stat failure %d\n", mypid, ret);
		return ret;
	}

	*size = (unsigned long) stat.st_size;
	return ret;
}

static int random_in_place_writer(void)
{
	int ret = 0;
	unsigned long size;
	off_t off;

	memset(block, 'i', BLKLEN);

	while (!die) {
		ret = get_i_size(fd, &size);
		if (ret)
			break;
		off = 0;

		if (size > BLKLEN)
			off = get_rand(0, size - BLKLEN);

		lseek(fd, off, SEEK_SET);

		logprint("write in place offset\t\t: %lu\n",
			 (unsigned long) off);

		ret = do_write(fd, block, BLKLEN);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

static int random_past_size_writer(void)
{
	int ret = 0;
	off_t off;

	memset(block, 'p', BLKLEN);

	while (!die) {
		off = get_rand(0, 3 * BLKLEN);

		lseek(fd, off, SEEK_END);

		logprint("write past i_size offset\t: %lu\n",(unsigned long) off);

		ret = do_write(fd, block, BLKLEN);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

static int truncate_caller(int up)
{
	int ret = 0;
	unsigned long size;
	off_t len;
	char *where = "down";

	if (up)
		where = "up";

	while (!die) {
		ret = get_i_size(fd, &size);
		if (ret)
			break;
		len = get_rand(0, size / 3);
		if (up)
			len += size;

		if (size && len) {
			logprint("truncate %s to size\t\t: %lu\n",
				 where, (unsigned long) len);

			ret = ftruncate(fd, len);
			if (ret == -1) {
				ret = errno;
				fprintf(stderr, "%d: truncate error %d\n",
					mypid, ret);
				break;
			}
		}

		random_sleep(400000);
	}

	return ret;

}

static int truncate_down(void)
{
	return truncate_caller(0);
}

static int truncate_up(void)
{
	return truncate_caller(1);
}

static int straddling_eof_writer(void)
{
	int ret = 0;
	unsigned long size;
	off_t off;

	memset(block, 's', BLKLEN);

	while (!die) {
		ret = get_i_size(fd, &size);
		if (ret)
			break;
		off = size;

		if (off >= BLKLEN)
			off -= get_rand(0, BLKLEN);

		lseek(fd, off, SEEK_SET);

		logprint("write straddling offset\t: %lu\n",
			 (unsigned long) off);

		ret = do_write(fd, block, BLKLEN);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

int main(int argc, char **argv)
{
	int ret = 0;
	int status;
	pid_t pid;
	char *fname;

	if (argc < 2) {
		fprintf(stderr, "%s <path>\n", argv[0]);
		return 1;
	}
	fname = argv[1];

	/* prep the file */
	fd = open(fname, O_RDWR|O_CREAT|O_TRUNC,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "Error %d opening \"%s\"\n", ret, fname);
		return ret;
	}
	close(fd);

	parent = mypid = getpid();

	/* setup the parent. */
	if (signal(SIGINT, signal_handler) == SIG_ERR) {
		fprintf(stderr, "Couldn't setup parent signal handler!\n");
		return 1;
	}

	ret = launch_child(fname, O_RDWR|O_APPEND, append_writer);
	if (!ret)
		ret = launch_child(fname, O_RDWR, random_in_place_writer);
	if (!ret)
		ret = launch_child(fname, O_RDWR, random_past_size_writer);
	if (!ret)
		ret = launch_child(fname, O_RDWR, truncate_down);
	if (!ret)
		ret = launch_child(fname, O_RDWR, truncate_up);
	if (!ret)
		ret = launch_child(fname, O_WRONLY, straddling_eof_writer);
	if (ret) {
		fprintf(stderr, "Error %d launching children\n", ret);
		goto kill_all;
	}

	while (1) {
		pid = wait(&status);
		if (die) {
			dprintf("die has been set, kill remaining children\n");
			break;
		}
		if (pid == -1) {
			ret = errno;
			if (ret != -ECHILD)
				fprintf(stderr,
					"Error %d returned from wait\n", ret);
			break;
		}

		if (!WIFEXITED(status)) {
			fprintf(stderr, "Child %u dies abormally - stopping "
				"test\n", pid);
			break;
		}

		dprintf("child %u dies with status %d\n", pid,
			WEXITSTATUS(status));

		if (WEXITSTATUS(status)) {
			ret = WEXITSTATUS(status);
			fprintf(stderr, "Child %u dies with error %d - "
				"stopping test\n", pid, ret);
			break;
		}
	}

kill_all:
	/* Kill the remaining children */
	if (ret != -ECHILD) {
		kill(0, SIGINT);
		dprintf("Killed children\n");
	}

	return ret == -ECHILD ? ret : 0;
}
