#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "fill_holes.h"
#include "reservations.h"


/*
3rd test program with verify util:
Given file name, size, optional #iters, optional logfile (uses stdout otherwise
create and ftruncate file to size
for each iter:
        pick random offset
        pick random size
        pick random 8 bit value
        print to log the triple
        write size bytes of value at offset
Verify program can parse log, sort triples into a list (removing and merging
triples as they overwrite each other). It can then read the file from 0 to
size, verifying it's contents.
*/

static void usage(void)
{
	printf("fill_holes [-f] [-m] [-u] [-i ITER] [-o LOGFILE] [-r REPLAYLOG] FILE SIZE\n"
	       "FILE is a path to a file\n"
	       "SIZE is in bytes and must always be specified, even with a REPLAYLOG\n"
	       "ITER defaults to 1000, unless REPLAYLOG is specified.\n"
	       "LOGFILE defaults to stdout\n"
	       "-f will result in logfile being flushed after every write\n"
	       "-m instructs the test to use mmap to write to FILE\n"
	       "-u will create an unwritten region instead of ftruncate\n"
	       "REPLAYLOG is an optional file to generate values from\n\n"
	       "FILE will be truncated to zero, then truncated out to SIZE\n"
	       "For each iteration, a character, offset and length will be\n"
	       "randomly generated or read from the optional REPLAYLOG and\n"
	       "written to FILE. The program ends after ITER iterations, or\n"
	       "until the end of the replay log, whichever comes first.\n"
	       "The exact patterns written will be logged such that\n"
	       "the log can be replayed by a verification program, or given\n"
	       "back to this software as a REPLAYLOG argument\n");

	exit(0);
}

#define MAX_WRITE_SIZE 32768
static char buf[MAX_WRITE_SIZE];

static unsigned int max_iter = 1000;
static unsigned int flush_output = 0;
static unsigned int create_unwritten = 0;
static char *fname = NULL;
static char *logname = NULL;
static char *replaylogname = NULL;
static unsigned long file_size;
static FILE *logfile = NULL;
static FILE *replaylogfile = NULL;
static int use_mmap = 0;
static void *mapped;

static int parse_opts(int argc, char **argv)
{
	int c, iter_specified = 0;

	while (1) {
		c = getopt(argc, argv, "umfi:o:r:");
		if (c == -1)
			break;

		switch (c) {
		case 'u':
			create_unwritten = 1;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'f':
			flush_output = 1;
			break;
		case 'i':
			max_iter = atoi(optarg);
			iter_specified = 1;
			break;
		case 'o':
			logname = optarg;
			break;
		case 'r':
			replaylogname = optarg;
			/*
			 * Trick the code into replaying until the log
			 * is empty.
			 */
			if (!iter_specified)
				max_iter = UINT_MAX;
			break;
		default:
			return EINVAL;
		}
	}
 
	if (argc - optind != 2)
		return EINVAL;

	fname = argv[optind];
	file_size = atol(argv[optind+1]);

	return 0;
}

static int resv_unwritten(int fd, uint64_t start, uint64_t len)
{
	int ret = 0;
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = 0;
	sr.l_start = start;
	sr.l_len = len;

	ret = ioctl(fd, OCFS2_IOC_RESVSP64, &sr);
	if (ret == -1) {
		fprintf(stderr, "ioctl error %d: \"%s\"\n",
			errno, strerror(errno));
		return -1;
	}

	return ret;
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

	if (create_unwritten) {
		ret = resv_unwritten(fd, 0, size);
		if (ret)
			return ret;
	}

	ret = ftruncate(fd, size);
	if (ret == -1) {
		close(fd);

		fprintf(stderr, "ftruncate error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	if (use_mmap) {
		mapped = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (mapped == MAP_FAILED) {
			close(fd);

			fprintf(stderr, "mmap error %d: \"%s\"\n", errno,
				strerror(errno));
			return -1;
		}
	}

	return fd;
}

static int open_logfile(void)
{
	if (!logname)
		logfile = stdout;
	else
		logfile = fopen(logname, "wa");
	if (!logfile) {
		fprintf(stderr, "Error %d creating logfile: %s\n", errno,
			strerror(errno));
		return EINVAL;
	}
	return 0;
}

static int replay_eof(void)
{
	if (!replaylogfile)
		return 0;

	return feof(replaylogfile);
}

static int open_replaylog(void)
{
	if (!replaylogname)
		return 0;

	replaylogfile = fopen(replaylogname, "r");
	if (!replaylogfile) {
		fprintf(stderr, "Error %d opening replay log: %s\n", errno,
			strerror(errno));
		return EINVAL;
	}
	return 0;
}

static void log_write(struct write_unit *wu)
{
	int fd;

	fprintf(logfile, "%c\t%lu\t%u\n", wu->w_char, wu->w_offset, wu->w_len);
	if (flush_output) {
		fflush(logfile);

		fd = fileno(logfile);
		fsync(fd);
	}
}

static unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + ((rand() % max) - min);
}

static void prep_rand_write_unit(struct write_unit *wu)
{
again:
	wu->w_char = RAND_CHAR_START + (char) get_rand(0, 52);
	wu->w_offset = get_rand(0, file_size - 1);
	wu->w_len = (unsigned int) get_rand(1, MAX_WRITE_SIZE);

	if (wu->w_offset + wu->w_len > file_size)
		wu->w_len = file_size - wu->w_offset;

	/* sometimes the random number might work out like this */
	if (wu->w_len == 0)
		goto again;

	assert(wu->w_char >= RAND_CHAR_START && wu->w_char <= 'z');
	assert(wu->w_len <= MAX_WRITE_SIZE);
	assert(wu->w_len > 0);
}

static int prep_write_unit(struct write_unit *wu)
{
	int ret;

	if (!replaylogfile) {
		prep_rand_write_unit(wu);
		return 0;
	}

	ret = fscanf(replaylogfile, "%c\t%lu\t%u\n", &wu->w_char,
		     &wu->w_offset, &wu->w_len);
	if (ret != 3) {
		fprintf(stderr, "input failure from replay log, ret %d, %d %s\n",
			ret, errno, strerror(errno));
		return -EINVAL;
	}

	return 0;
}

int do_write(int fd, struct write_unit *wu)
{
	int ret;

	if (use_mmap) {
		memset(mapped + wu->w_offset, wu->w_char, wu->w_len);
		return 0;
	}

	memset(buf, wu->w_char, wu->w_len);
	ret = pwrite(fd, buf, wu->w_len, wu->w_offset);
	if (ret == -1) {
		fprintf(stderr, "write error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret, i, fd;
	struct write_unit wu;

	if (argc < 3) {
		usage();
		return 1;
	}

	ret = parse_opts(argc, argv);
	if (ret) {
		usage();
		return 1;
	}

	fd = prep_file(fname, file_size);
	if (fd == -1)
		return 1;

	ret = open_logfile();
	if (ret)
		return 1;

	ret = open_replaylog();
	if (ret)
		return 1;

	srand(getpid());

	for(i = 0; (i < max_iter) && !replay_eof(); i++) {
		ret = prep_write_unit(&wu);
		if (ret)
			return 1;

		log_write(&wu);

		ret = do_write(fd, &wu);
		if (ret)
			return 1;
	}

	return 0;
}
