/*
 * Verify I/O to/from files small enough to hold inline data. Includes
 * some tests intended to force an inode out to an extent list.
 *
 * [I] Tests of inline-data code. All tests read back the pattern
 *     written to verify data integrity.
 *    1) Write a pattern, read it back.
 *    2) Shrink file by some bytes
 *    3) Extend file again
 *    4) Mmap file and read pattern
 *    5) Reserve space inside of region
 *    6) Punch hole in the middle
 *
 * [II] Tests intended to force data out of inode. Before each test,
 *      we'll have to truncate to zero and re-write file.
 *    1) Seek past EOF, write pattern.
 *    2) Mmap and do a write inside of i_size
 *    3) Extend file past min size
 *    4) Resrve space past min size
 *
 * XXX: This could easily be turned into an mpi program, where a
 * second node does the verification step.
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "reservations.h"

#define PATTERN_SZ 8192
static char *pattern = NULL;
static unsigned int max_inline_size;
static char *read_buf = NULL;

static unsigned long page_size;
static unsigned int blocksize;
static char *file_name;

static unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + ((rand() % max) - min);
}

static inline char rand_char(void)
{
	return 'A' + (char) get_rand(0, 52);
}

static void fill_pattern(int size)
{
	int i;

	/*
	 * Make sure that anything in the buffer past size is zero'd,
	 * as a regular file should look.
	 */
	memset(pattern, 0, PATTERN_SZ);

	for(i = 0; i < size; i++)
		pattern[i] = rand_char();
}

static int truncate_pattern(int fd, unsigned int old_size,
			    unsigned int new_size)
{
	int bytes = old_size - new_size;
	int ret;

	memset(pattern + new_size, 0, bytes);

	ret = ftruncate(fd, new_size);
	if (ret == -1) {
		fprintf(stderr, "ftruncate error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

static int extend_pattern(int fd, unsigned int old_size,
			  unsigned int new_size)
{
	int bytes = new_size - old_size;
	int ret;

	memset(pattern + old_size, 0, bytes);

	ret = ftruncate(fd, new_size);
	if (ret == -1) {
		fprintf(stderr, "ftruncate error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

static int try_ioctl(int which, int fd, unsigned int offset, unsigned int count)
{
	struct ocfs2_space_resv sr;

	memset(&sr, 0, sizeof(sr));
	sr.l_whence = SEEK_SET;
	sr.l_start = offset;
	sr.l_len = count;

	return ioctl(fd, which, &sr);
}

static int try_reserve(int fd, unsigned int offset, unsigned int count)
{
	int ret;

	ret = try_ioctl(OCFS2_IOC_RESVSP, fd, offset, count);
	if (ret == -1) {
		ret = errno;
		if (ret == ENOTTY)
			return ret;
		fprintf(stderr, "IOC_RESVSP error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

static int try_punch_hole(int fd, unsigned int offset, unsigned int count)
{
	int ret;

	memset(pattern + offset, 0, count);

	ret = try_ioctl(OCFS2_IOC_UNRESVSP, fd, offset, count);
	if (ret == -1) {
		ret = errno;
		if (ret == ENOTTY)
			return ret;
		fprintf(stderr, "IOC_UNRESVSP error %d: \"%s\"\n",
			ret, strerror(ret));
		return -1;
	}

	return 0;
}

static int mmap_write_at(int fd, const char *buf, size_t count, off_t offset)
{
	unsigned int mmap_size = page_size;
	unsigned int size = count + offset;
	int i, j, ret;
	char *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (write) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	j = 0;
	for(i = offset; i < size; i++)
		region[i] = buf[j++];

	munmap(region, mmap_size);

	return 0;
}

static int write_at(int fd, const void *buf, size_t count, off_t offset)
{
	off_t err;
	int ret;

	err = lseek(fd, offset, SEEK_SET);
	if (err == ((off_t)-1)) {
		ret = errno;
		fprintf(stderr, "seek error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	ret = write(fd, buf, count);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "write error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}
	if (ret != count) {
		fprintf(stderr, "Short write: wanted %d, got %d\n", count, ret);
		return -1;
	}

	return 0;
}

static int prep_file_no_fill(unsigned int size, int open_direct)
{
	int fd, ret;
	int flags = O_TRUNC|O_CREAT|O_RDWR;
	size_t count = size;

	if (open_direct) {
		flags |= O_DIRECT;
		count = blocksize;
	}

	fd = open(file_name, flags,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "open error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	ret = write_at(fd, pattern, count, 0);
	if (ret)
		return ret;

	if (count > size) {
		ret = ftruncate(fd, size);
		if (ret) {
			ret = errno;
			fprintf(stderr, "truncate error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}
	}

	return fd;
}

static int prep_file(unsigned int size)
{
	fill_pattern(size);

	return prep_file_no_fill(size, 0);
}

static int verify_pattern(int size, char *buf)
{
	int i;

	for(i = 0; i < size; i++) {
		if (buf[i] != pattern[i]) {
			fprintf(stderr, "Verify failed at byte: %d\n", i);
			return -1;
		}
	}
	return 0;
}

static int __verify_pattern_fd(int fd, unsigned int size, int direct_read)
{
	off_t err;
	int ret;
	unsigned int rd_size = size;

	if (direct_read)
		rd_size = blocksize;

	err = lseek(fd, 0, SEEK_SET);
	if (err == ((off_t)-1)) {
		ret = errno;
		fprintf(stderr, "seek error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	ret = read(fd, read_buf, rd_size);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "read error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}
	if (ret != size) {
		fprintf(stderr, "Short read: wanted %d, got %d\n", size, ret);
		return -1;
	}

	return verify_pattern(size, read_buf);
}

static int verify_pattern_fd(int fd, unsigned int size)
{
	return __verify_pattern_fd(fd, size, 0);
}

static int verify_pattern_mmap(int fd, unsigned int size)
{
	int ret;
	unsigned int mmap_size = page_size;
	void *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_READ, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (read) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	ret = verify_pattern(size, region);

	munmap(region, mmap_size);

	return 0;
}

static void usage(void)
{
	printf("Usage: inline-data [blocksize] [FILE]\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "files with inline data.\n\n"
	       "blocksize is the blocksize of the underlying file system and\n"
	       "must be specified.\n"
	       "FILE is the name of a file which will be used for testing.\n");
}

static int parse_opts(int argc, char **argv)
{
	if (argc < 3)
		return 1;

	blocksize = atoi(argv[1]);
	file_name = argv[2];

	switch (blocksize) {
	case 4096:
		max_inline_size = 3896;
		break;
	case 2048:
		max_inline_size = 1848;
		break;
	case 1024:
		max_inline_size = 824;
		break;
	case 512:
		max_inline_size = 312;
		break;
	default:
		fprintf(stderr, "Invalid blocksize, %u\n", blocksize);
		return 1;
	}

	printf("Blocksize:\t\t%d\nMax Inline Data Size:\t%d\nFilename:\t\t%s\n\n",
	       blocksize, max_inline_size, file_name);

	return 0;
}

int main(int argc, char **argv)
{
	int ret, fd, new_size, old_size, count;

	if (parse_opts(argc, argv)) {
		usage();
		return EINVAL;
	}

	ret = posix_memalign((void *)&pattern, blocksize, PATTERN_SZ);
	if (ret) {
		fprintf(stderr, "posix_memalign error %d: \"%s\"\n",
			ret, strerror(ret));
		return 1;
	}

	ret = posix_memalign((void *)&read_buf, blocksize, PATTERN_SZ);
	if (ret) {
		fprintf(stderr, "posix_memalign error %d: \"%s\"\n",
			ret, strerror(ret));
		return 1;
	}

	srand(getpid());
	page_size = sysconf(_SC_PAGESIZE);

	printf("Test  1: Write basic pattern\n");
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;

	printf("Test  2: Rewrite portion of basic pattern\n");
	ret = write_at(fd, pattern + 10, 100, 10);
	if (ret)
		return 1;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return 1;

	printf("Test  3: Reduce size with truncate\n");
	new_size = max_inline_size - 180;
	ret = truncate_pattern(fd, max_inline_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return ret;

	printf("Test  4: Extend file\n");
	old_size = new_size;
	new_size = max_inline_size;
	ret = extend_pattern(fd, old_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return ret;

	printf("Test  5: Double write, both extending\n");
	close(fd);
	fill_pattern(200);
	fd = prep_file_no_fill(100, 0);
	if (fd < 0)
		return 1;
	ret = write_at(fd, pattern + 50, 150, 50);
	if (ret)
		return 1;
	ret = verify_pattern_fd(fd, 200);
	if (ret)
		return ret;

	printf("Test  6: Fsync\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	ret = fsync(fd);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;

	printf("Test  7: Fdatasync\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	ret = fdatasync(fd);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;

	printf("Test  8: Mmap reads\n");
	close(fd);
        fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	ret = verify_pattern_mmap(fd, max_inline_size);
	if (ret)
		return ret;

	printf("Test  9: Reserve space\n");
	ret = try_reserve(fd, 0, max_inline_size);
	if (ret != ENOTTY) {
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			return ret;
	}

	printf("Test  10: Punch hole\n");
	ret = try_punch_hole(fd, 10, 100);
	if (ret != ENOTTY) {
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, max_inline_size);
		if (ret)
			return ret;
	}

	printf("Test  11: Force expansion to extents via large write\n");
	close(fd);
	fill_pattern(PATTERN_SZ);
	fd = prep_file_no_fill(max_inline_size, 0);
	if (fd < 0)
		return 1;
	count = PATTERN_SZ - max_inline_size;
	ret = write_at(fd, &pattern[max_inline_size], count, max_inline_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, PATTERN_SZ);
	if (ret)
		return ret;

	printf("Test 12: Force expansion to extents via mmap write\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	fill_pattern(max_inline_size);
	ret = mmap_write_at(fd, pattern, max_inline_size, 0);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, max_inline_size);
	if (ret)
		return ret;

	printf("Test 13: Force expansion to extents via large extend\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return fd;
	old_size = max_inline_size;
	new_size = 2*max_inline_size;
	ret = extend_pattern(fd, old_size, new_size);
	if (ret)
		return ret;
	ret = verify_pattern_fd(fd, new_size);
	if (ret)
		return 1;

	printf("Test 14: Force expansion to extents via large reservation\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	new_size = PATTERN_SZ;
	ret = try_reserve(fd, 0, new_size);
	if (ret != ENOTTY) {
		if (ret)
			return ret;
		ret = extend_pattern(fd, max_inline_size, new_size);
		if (ret)
			return ret;
		ret = verify_pattern_fd(fd, new_size);
		if (ret)
			return ret;
	}

	printf("Test 15: O_DIRECT read\n");
	close(fd);
	fd = prep_file(max_inline_size);
	if (fd < 0)
		return 1;
	close(fd);
	fd = open(file_name, O_RDWR|O_DIRECT);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "open (direct) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}
	ret = __verify_pattern_fd(fd, max_inline_size, 1);
	if (ret)
		return 1;

	printf("Test 16: O_DIRECT write\n");
	close(fd);
	fill_pattern(max_inline_size);
	fd = prep_file_no_fill(max_inline_size, 1);
	if (fd < 0)
		return 1;
	ret = __verify_pattern_fd(fd, max_inline_size, 1);
	if (ret)
		return 1;
	close(fd);

	printf("All File I/O Tests Passed\n");

	unlink(file_name);

	return ret;
}
