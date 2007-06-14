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

static unsigned int seconds = 0;
#define DEFAULT_BLKLEN 8102
static unsigned int blklen = DEFAULT_BLKLEN;

static pid_t mypid, parent;
static int die = 0;
static int fd;
static char *block;
static int logfd = STDOUT_FILENO;
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
static char hostn[MAXHOSTNAMELEN];

static void __logprint(const char *fmt, ...)
{
	int len;
	static char str[4096];
	va_list ap;

	va_start(ap, fmt);

	len = vsnprintf(str, 4096, fmt, ap);
	if (len == -1) {
		len = errno;
		fprintf(stderr, "%s: Can't log, error %d\n", hostn, len);
		return;
	}

	write(logfd, str, len);
}

#define logprint(fmt, args...) __logprint("%s: [%u]: "fmt, hostn, mypid, args)

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

	dprintf("%s:%u: sleep %u usec\n", hostn, mypid, usec);
	usleep(usec);
}

static void signal_handler(int sig)
{
	dprintf("%s:%u: signal %d recieved\n", hostn, mypid, sig);
	if (sig == SIGALRM)
		logprint("Alarm fired! (%d)\n", sig);

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
				"%s: Error %d opening \"%s\"\n", hostn, 
				ret, fname);
			exit(ret);
		}

		mypid = getpid();
		srand(mypid);
		ret = newmain();
		exit(ret);
	}
	if (pid == -1) {
		ret = errno;
		fprintf(stderr, "%s: could not fork: %d\n", hostn, ret);
	}

	return ret;
}

static int do_write(int fd, const char *buf, unsigned int len)
{
	int written, ret = 0;

	written = write(fd, buf, len);
	if (written == -1) {
		ret = errno;
		fprintf(stderr, "%s:%d: append write failure %d len[%d]\n", 
			hostn, mypid, ret, len);
	} else if (written < len)
		fprintf(stderr, "%s:%d: short write! len = %u, written = %u\n",
			hostn, mypid, len, written);

	return ret;
}

static int append_writer(void)
{
	int ret = 0;
	int len;

	memset(block, 'a', blklen);

	while (!die) {
		len = get_rand(1, blklen);
		logprint("append write len             : %d\n", len);

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
		fprintf(stderr, "%s:%d: stat failure %d\n", 
		        hostn, mypid, ret);
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

	memset(block, 'i', blklen);

	while (!die) {
		ret = get_i_size(fd, &size);
		if (ret)
			break;
		off = 0;

		if (size > blklen)
			off = get_rand(0, size - blklen);

		lseek(fd, off, SEEK_SET);

		logprint("write in place offset        : %lu\n",
			 (unsigned long) off);

		ret = do_write(fd, block, blklen);
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

	memset(block, 'p', blklen);

	while (!die) {
		off = get_rand(0, 3 * blklen);

		lseek(fd, off, SEEK_END);

		logprint("write past i_size offset     : %lu\n",
			 (unsigned long) off);

		ret = do_write(fd, block, blklen);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

/* Give us some leeway so that the other writers don't have to check
 * that the file size doesn't grow too large */
#define MAX_TRUNCATE_SIZE (2147483647 - (100 * blklen))

static int truncate_caller(int up)
{
	int ret = 0;
	unsigned long size;
	off_t len;
	char *where = "down";

	if (up)
		where = " up ";

	while (!die) {

		do {
			ret = get_i_size(fd, &size);
			if (ret)
				goto out;

			len = get_rand(0, size / 3);
			if (up)
				len += size;
		} while ((len > MAX_TRUNCATE_SIZE) && (len < 0));

		if (size && len) {
			logprint("truncate %s to size        : %d\n",
				 where, len);

			ret = ftruncate(fd, len);
			if (ret == -1) {
				ret = errno;
				fprintf(stderr, "%s:%d: truncate error %d\n",
					hostn, mypid, ret);
				break;
			}
		}

		random_sleep(200000 + 200000 * up);
	}

out:
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

	memset(block, 's', blklen);

	while (!die) {
		ret = get_i_size(fd, &size);
		if (ret)
			break;
		off = size;

		if (off >= blklen)
			off -= get_rand(0, blklen);

		lseek(fd, off, SEEK_SET);

		logprint(" write straddling offset      : %lu\n",
			 (unsigned long) off);

		ret = do_write(fd, block, blklen);
		if (ret)
			break;

		random_sleep(100000);
	}

	return ret;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: write_torture [-s <seconds>] [-b <blocksize>] <path>\n"
		"<seconds> defaults to '0' (run forever)\n"
		"<blocksize> defaults to 8092\n"
		"For best results choose a <blocksize> value that is not a\n"
		"multiple of the file system cluster size.\n");
}

static int parse_opts(int argc, char **argv, char **fname)
{
	int c;

	*fname = NULL;

	while (1) {
		c = getopt(argc, argv, "s:b:");
		if (c == -1)
			break;

		switch (c) {
		case 's':
			seconds = atoi(optarg);
			break;
		case 'b':
			blklen = atoi(optarg);
			break;
		default:
			return EINVAL;
		}
	}
 
	if (argc - optind != 1)
		return EINVAL;

	*fname = argv[optind];

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	int status;
	pid_t pid;
	char *fname;
	printf("will get hostname\n");
        gethostname(hostn, MAXHOSTNAMELEN);
	printf("got hostname\n");


	if (argc < 2) {
		usage();
		return 1;
	}

	if (parse_opts(argc, argv, &fname)) {
		usage();
		return 1;
	}

	if (seconds)
		printf("%s: Will bound the test at about %u seconds\n", 
			hostn, seconds);
	if (blklen != DEFAULT_BLKLEN)
		printf("%s: Using block size of %u bytes\n", hostn, blklen);

	block = malloc(blklen);
	if (!block) {
		fprintf(stderr, "%s: Not enough memory to allocate %u bytes\n",
			hostn, blklen);
		return ENOMEM;
	}

	/* prep the file */
	fd = open(fname, O_RDWR|O_CREAT|O_TRUNC,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "%s: Error %d opening \"%s\"\n", 
		        hostn, ret, fname);
		return ret;
	}
	close(fd);

	parent = mypid = getpid();

	/* setup the parent. */
	if (signal(SIGINT, signal_handler) == SIG_ERR) {
		fprintf(stderr, "%s: Couldn't setup parent signal handler!\n",
		        hostn);
		return 1;
	}

	/* We don't care. Getting back a short write is just fine. */
	if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR) {
		fprintf(stderr, "%s: Couldn't ignore SIGXFSZ!\n", hostn);
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
		fprintf(stderr, "%s: Error %d launching children\n", 
		        hostn, ret);
		goto kill_all;
	}

	if (seconds) {
		if (signal(SIGALRM, signal_handler) == SIG_ERR) {
			fprintf(stderr, "%s: Couldn't setup SIGALRM handler!\n",
			        hostn);
			goto kill_all;
		}

		ret = alarm(seconds);
		if (ret) {
			fprintf(stderr, "%s: alarm(2) returns %u\n", 
				hostn, ret);
			goto kill_all;
		}
	}

	while (1) {
		status = 0;
		pid = waitpid(-1, &status, WNOHANG);
		if (die) {
			dprintf("%s: die has been set, kill remaining "
				"children\n", hostn);
			break;
		}
		if (pid == -1) {
			ret = errno;
			if (ret != -ECHILD)
				fprintf(stderr,
					"%s: Error %d returned from wait\n", 
					hostn, ret);
			break;
		}

		if (!WIFEXITED(status)) {
			fprintf(stderr, 
			        "%s: Child %u dies abormally - stopping "
				"test\n", hostn, pid);
			if (WIFSIGNALED(status))
				fprintf(stderr, 
					"%s: It couldn't catch sig %d\n",
					hostn, WTERMSIG(status));
			break;
		}

		if (pid)
			dprintf("%s:child %u dies with status %d\n", 
				hostn, pid, WEXITSTATUS(status));

		if (WEXITSTATUS(status)) {
			ret = WEXITSTATUS(status);
			fprintf(stderr, "%s: Child %u dies with error %d - "
				"stopping test\n", hostn, pid, ret);
			break;
		}

		/* check every half a second */
		usleep(500000);
	}

kill_all:
	/* Kill the remaining children */
	if (ret != -ECHILD) {
		kill(0, SIGINT);
		dprintf("%s: Killed children\n", hostn);
	}

	return ret == -ECHILD ? ret : 0;
}
