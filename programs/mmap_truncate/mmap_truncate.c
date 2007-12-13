#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_CSIZE_BITS	12

static unsigned int clustersize_bits = DEFAULT_CSIZE_BITS;
#define clustersize		(1 << clustersize_bits)
static char *fname;
static void *mapped;
static unsigned int seconds = 300;
static int die = 0;

static void usage(void)
{
	printf("Usage: mmap_truncate [-c csize_bits] [-s seconds] FILE\n\n"
	       "Stress file system stability by testing end of file boundary\n"
	       "conditions with mmap by racing truncates and writes to a\n"
	       "shared writeable region.\n\n"
	       "FILE\ta path to a file that will be created and truncated if "
	       "it already exists.\n"
	       "-c\tsets the fs clustersize used by the test.\n"
	       "\tThe default is to use a csize_bits of 12 (4096 bytes).\n"
	       "-s\tsets the number of seconds to run the test.\n"
	       "\tThe default is to run for 300 seconds.\n");
	exit(0);
}

static int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "c:s:");
		if (c == -1)
			break;

		switch (c) {
		case 'c':
			clustersize_bits = atoi(optarg);
			break;
		case 's':
			seconds = atoi(optarg);
			break;
		default:
			return EINVAL;
		}
	}
 
	if (argc - optind != 1)
		return EINVAL;

	fname = argv[optind];

	return 0;
}

static void signal_handler(int sig);

static int setup_sighandler(int sig)
{
	if (signal(sig, signal_handler) == SIG_ERR) {
		fprintf(stderr, "Couldn't setup signal handler!\n");
		return -1;
	}

	return 0;
}

static void signal_handler(int sig)
{
	if (sig == SIGALRM) {
		printf("Alarm fired, exiting\n");
		die = 1;
	}
	if (setup_sighandler(SIGBUS))
		abort();
}

static int setup_alarm(unsigned int secs)
{
	int ret;

	ret = alarm(seconds);
	if (ret) {
		fprintf(stderr, "alarm error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	return 0;
}

static int truncate_file(int fd, unsigned long size)
{
	int ret;

	ret = ftruncate(fd, size);
	if (ret == -1) {
		fprintf(stderr, "ftruncate error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	return 0;
}

static int prep_file(char *name, unsigned long size)
{
	int ret, fd;

	fd = open(name, O_RDWR|O_CREAT|O_TRUNC,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		fprintf(stderr, "open error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	ret = truncate_file(fd, size);
	if (ret)
		return -1;

	mapped = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapped == MAP_FAILED) {
		close(fd);

		fprintf(stderr, "mmap error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	return fd;
}

static void random_sleep(unsigned int max_usecs)
{
	int r = rand();
	unsigned int usec;

	usec = r % max_usecs;

	usleep(usec);
}

static void mmap_process(unsigned long file_size)
{
	unsigned long offset = file_size - 1;

	while (1) {
		if (die)
			return;

		random_sleep(50);

		memset(mapped + offset, 'a', 1);
	}
}

static void truncating_process(int fd, unsigned long file_size,
			       unsigned long trunc_size)
{
	int ret;

	while (1) {
		if (die)
			return;

		ret = truncate_file(fd, trunc_size);
		if (ret)
			abort();

		ret = truncate_file(fd, file_size);
		if (ret)
			abort();
	}
}

int main(int argc, char *argv[])
{
	int ret, fd;
	unsigned long trunc_size, file_size;

	if (argc < 2) {
		usage();
		return 1;
	}

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return 1;
	}

	if (setup_sighandler(SIGBUS))
		return 1;

	if (setup_sighandler(SIGALRM))
		return 1;

	file_size = 2 * clustersize;
	trunc_size = file_size - clustersize;

	fd = prep_file(fname, file_size);
	if (fd == -1)
		return 1;

	if (setup_alarm(seconds))
		return 1;

	printf("Running test against file \"%s\" with cluster size %u "
	       "bytes for %u seconds.\n", fname, clustersize, seconds);

	if (fork()) {
		mmap_process(file_size);
		kill(0, SIGINT);
	} else
		truncating_process(fd, file_size, trunc_size);

	return 0;
}
