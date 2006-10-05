#ifndef FILL_HOLES_H
#define FILL_HOLES_H

#define MAX_WRITE_SIZE 32768

struct write_unit {
	char w_char;
	unsigned long w_offset;
	unsigned int  w_len;
};

#endif
