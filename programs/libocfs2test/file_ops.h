/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file_ops.h
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef FILE_OPS_H
#define FILE_OPS_H

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <linux/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include <ocfs2/ocfs2.h>

#define OCFS2_MAX_FILENAME_LEN  255
#define MAX_WRITE_SIZE		32768
#define RAND_CHAR_START		'A'
#define MAGIC_HOLE_CHAR		(RAND_CHAR_START - 1)

#define FILE_RW_FLAGS           (O_CREAT|O_RDWR)
#define FILE_RO_FLAGS           (O_RDONLY)
#define FILE_AP_FLAGS		(O_CREAT|O_RDWR|O_APPEND)

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

struct write_unit {
	char w_char;
	unsigned long w_offset;
	unsigned int  w_len;
};

unsigned long get_rand(unsigned long min, unsigned long max);
char get_rand_char(void);
unsigned int get_rand_nam(char *str, unsigned int least, unsigned int most);

int read_at(int fd, void *buf, size_t count, off_t offset);
int read_at_file(char *pathname, void *buf, size_t count, off_t offset);
int mmap_read_at(int fd, char *buf, size_t count, off_t offset,
		 size_t page_size);
int mmap_read_at_file(char *pathname, void *buf, size_t count, off_t offset,
		      size_t page_size);
int write_at(int fd, const void *buf, size_t count, off_t offset);
int write_at_file(char *pathname, const void *buf, size_t count, off_t offset);
int mmap_write_at(int fd, const char *buf, size_t count, off_t offset,
		  size_t page_size);
int mmap_write_at_file(char *pathname, const void *buf, size_t count,
		       off_t offset, size_t page_size);

int reflink(const char *oldpath, const char *newpath);

int do_write(int fd, struct write_unit *wu, int write_method, size_t page_size);
int do_write_file(char *fname, struct write_unit *wu, int write_method,
		  size_t page_size);

int get_bs_cs(char *device_name, unsigned int *bs, unsigned long *cs,
	      unsigned long *max_inline_sz);

int punch_hole(int fd, struct write_unit *wu);
#endif
