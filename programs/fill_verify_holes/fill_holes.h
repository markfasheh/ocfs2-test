#ifndef FILL_HOLES_H
#define FILL_HOLES_H

#include <stdint.h>

#define MAX_WRITE_SIZE		32768
#define RAND_CHAR_START		'A'
#define MAGIC_HOLE_CHAR		(RAND_CHAR_START - 1)

struct fh_write_unit {
	char		w_char;
	uint64_t	w_offset;
	uint32_t	w_len;
};

#endif
