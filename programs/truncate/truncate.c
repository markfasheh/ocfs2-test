#define DO_TRUNCATE64

#ifdef DO_TRUNCATE64

#define read_size atoll
#define do_truncate truncate64
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#else
#define read_size atol
#define do_truncate truncate
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>


int main(int argc, char ** argv) {
#ifdef DO_TRUNCATE64
	loff_t newsize = 0; /* long long */
#else
	off_t newsize = 0;
#endif
	char *filename;

	if (argc < 3) {
		printf("Usage: %s <path> <size>\n", argv[0]);
		printf("  size is expected to be in bytes\n");
		return 1;
	}
	filename = argv[1];
	newsize = read_size(argv[2]);

	printf("Truncating %s to %lld\n", filename,
	       (unsigned long long) newsize);
	if (do_truncate(filename, newsize) == -1) {
		fprintf(stderr, "Could not truncate %s to %lld bytes: %s\n", 
			filename, (unsigned long long) newsize,
			strerror(errno));
		return 1;
	}

	return 0;
}
