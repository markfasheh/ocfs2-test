#define DO_TRUNCATE64

#ifdef DO_TRUNCATE64
#define read_size atoll
#define do_truncate truncate64
#define do_ftruncate ftruncate64
#define offset_t loff_t
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#else
#define read_size atol
#define do_truncate truncate
#define do_ftruncate ftruncate
#define offset_t off_t
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <inttypes.h>


int main(int argc, char ** argv) {
	uint64_t newsize = 0;
	char *filename;
	int fd;

	if (argc < 3) {
		printf("Usage: %s FILENAME NEWSIZE\n", argv[0]);
		printf("  NEWSIZE is expected to be in bytes\n");
		return(0);
	}
	filename = argv[1];
	newsize = read_size(argv[2]);

	fd = open(filename, O_WRONLY|O_DIRECT);
	if (fd < 0) {
		fprintf(stderr, "Could not open %s: %s\n", 
			filename, strerror(errno));
		return(1);
	}

	printf("Truncating %s to  %"PRIu64" \n", filename,
	       newsize);
	if (do_ftruncate(fd, newsize) == -1) {
		fprintf(stderr, "Could not truncate %s to  %"PRIu64" bytes: %s\n", 
			filename, newsize,
			strerror(errno));
		return(1);
	}

	return(0);
}
