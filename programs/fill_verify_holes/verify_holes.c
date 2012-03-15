/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * verify_holes.c
 *
 * Copyright (C) 2006, 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <linux/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ocfs2/ocfs2.h>
#include <ocfs2/kernel-rbtree.h>

#include "fill_holes.h"

struct rb_root	chunk_root = RB_ROOT;

struct file_chunk {
	struct fh_write_unit	fc_write;
	struct rb_node		fc_node;
};

static struct file_chunk *init_chunk = NULL;
static char buf[MAX_WRITE_SIZE];
static int verbose = 0;

static void vh_usage(void)
{
	printf("verify_holes [-v] LOGFILE FILE\n"
	       "FILE is a path to a file\n"
	       "LOGFILE is a path to a log file\n"
	       "Use LOGFILE to verify the patterns written by fill_holes\n"
	       "in FILE\n"
		"-v will turn on verbose mode\n");

	exit(0);
}

static int vh_open_files(char *log, FILE **logfp, char *testfile, int *testfd)
{
	int fd;

	*logfp = fopen(log, "r");
	if (!*logfp) {
		fprintf(stderr, "error %d opening \"%s\": \"%s\"\n", errno,
			log, strerror(errno));
		return -1;
	}

	fd = open(testfile, O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "error %d opening \"%s\": \"%s\"\n", errno,
			testfile, strerror(errno));
		return -1;
	}

	*testfd = fd;

	return 0;
}

static int vh_get_i_size(int fd, unsigned long *size)
{
	struct stat stat;
	int ret;

	ret = fstat(fd, &stat);
	if (ret == -1) {
		ret = errno;
		fprintf(stderr, "stat failure %d: %s\n", ret, strerror(ret));
		return ret;
	}

	*size = (unsigned long) stat.st_size;
	return ret;
}

static struct file_chunk *vh_alloc_chunk(void)
{
	struct file_chunk *f = calloc(1, sizeof(*init_chunk));
	if (!f) {
		fprintf(stderr, "malloc error.\n");
		exit(1);
	}

	return f;
}

static void vh_print_extent(const char *pre, FILE *where, struct fh_write_unit *wu)
{
	char ch[3] = "\\0\0";

	if (wu->w_char != '\0') {
		ch[0] = wu->w_char;
		ch[1] = '\0';
	}

	fprintf(where, "%s: {%s, %"PRIu64", %u} (%"PRIu64")\n", pre, ch,
		wu->w_offset, wu->w_len, wu->w_offset + wu->w_len);
}

static int recurse_level = 0;

static void vh_insert_chunk(struct file_chunk *chunk)
{
	struct rb_node **p;
	struct rb_node *parent;
	struct file_chunk *tmp;
	unsigned long off = chunk->fc_write.w_offset;
	unsigned int len = chunk->fc_write.w_len;
	unsigned long end = off + len;

	/*
	 * This function should never recurse more than once - the
	 * second insert (if called) should _always_ be to a region
	 * which has been pre-cleared
	 */
	recurse_level++;
	if (recurse_level > 2) {
		fprintf(stderr, "Recursion level too high\n");
		abort();
	}

restart_search:
#ifdef DEBUG_INSERT
	vh_print_extent("insert chunk", stdout, &chunk->fc_write);
#endif
	p = &chunk_root.rb_node;
	parent = NULL;

	while(*p) {
		unsigned long tmpoff;
		unsigned int tmplen;
		unsigned long tmpend;
		struct fh_write_unit *wu;
		parent = *p;

		tmp = rb_entry(parent, struct file_chunk, fc_node);

		wu = &tmp->fc_write;
		tmpoff = wu->w_offset;
		tmplen = wu->w_len;
		tmpend = tmpoff + tmplen;

		if (off <= tmpoff && end >= tmpend) {
#ifdef DEBUG_INSERT
			printf("Fully encompasses another extent\n");
			vh_print_extent("extent found", stdout, wu);
#endif

			/* We fully encompass this extent */
			rb_erase(&tmp->fc_node, &chunk_root);
			free(tmp);

			goto restart_search;
		} else if (off > tmpoff && end < tmpend) {
			struct file_chunk *tmp2 = vh_alloc_chunk();
			char tmpch = wu->w_char;
			/* We are in the middle of this extent */
#ifdef DEBUG_INSERT
			printf("Split existing extent\n");
			vh_print_extent("extent found", stdout, wu);
#endif

			rb_erase(&tmp->fc_node, &chunk_root);

			wu->w_len = off - tmpoff;

			vh_insert_chunk(tmp);

			wu = &tmp2->fc_write;

			wu->w_offset = end;
			wu->w_len = tmpend - wu->w_offset;
			wu->w_char = tmpch;

			vh_insert_chunk(tmp2);
			goto restart_search;
		} else if (off <= tmpoff && end <= tmpend && end > tmpoff) {
			/* We straddle the left side of this extent */
#ifdef DEBUG_INSERT
			printf("Left straddle existing extent\n");
			vh_print_extent("extent found", stdout, wu);
#endif

			rb_erase(&tmp->fc_node, &chunk_root);

			wu->w_offset = end;
			wu->w_len = tmpend - wu->w_offset;

			vh_insert_chunk(tmp);
			goto restart_search;
		} else if (off > tmpoff && off < tmpend && end >= tmpend) {
#ifdef DEBUG_INSERT
			printf("Right straddle existing extent\n");
			vh_print_extent("extent found", stdout, wu);
#endif

			/* We straddle the right side of this extent */
			rb_erase(&tmp->fc_node, &chunk_root);

			wu->w_len = off - tmpoff;

			vh_insert_chunk(tmp);
			goto restart_search;
		} else if (off < tmpoff)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&chunk->fc_node, parent, p);
	rb_insert_color(&chunk->fc_node, &chunk_root);

	recurse_level--;
}

static void vh_init_tree(unsigned long file_size)
{
	init_chunk = vh_alloc_chunk();

	init_chunk->fc_write.w_char = '\0';
	init_chunk->fc_write.w_len = file_size;
	init_chunk->fc_write.w_offset = 0;

	vh_insert_chunk(init_chunk);
}

static int vh_read_log(FILE *logfile)
{
	int ret;
	unsigned int line = 0;
	struct file_chunk *chunk;
	struct fh_write_unit *wu;

	while (1) {
		chunk = vh_alloc_chunk();
		wu = &chunk->fc_write;

		ret = fscanf(logfile, "%c\t%"PRIu64"\t%u\n", &wu->w_char,
			     &wu->w_offset, &wu->w_len);
		if (ret == 3) {
			if (wu->w_char == MAGIC_HOLE_CHAR)
				wu->w_char = '\0';
			vh_insert_chunk(chunk);
			line++;
			continue;
		}

		if (ret == EOF)
			return 0;

		fprintf(stderr, "input failure at log file line %u\n", line);
		ret = EINVAL;
		break;
	}

	return ret;
}

static int vh_check_chunk(struct fh_write_unit *wu, int fd)
{
	unsigned int len = wu->w_len;
	unsigned int ret;
	uint64_t off = wu->w_offset;
	int i;

	if (verbose)
		vh_print_extent("check chunk", stdout, wu);

	while (len) {
		unsigned int count;

		count = len;
		if (len > MAX_WRITE_SIZE)
			count = MAX_WRITE_SIZE;

		ret = pread(fd, buf, count, off);
		if (ret == -1) {
			ret = errno;
			fprintf(stderr, "read error %d: %s\n",
				ret, strerror(ret));
			return ret;
		}

		if (ret == 0) {
			fprintf(stderr, "premature end of file\n");
			return EINVAL;
		}

		if (ret != count) {
			fprintf(stderr, "short read. asked %u, read %u\n",
				count, ret);
			return EINVAL;
		}

		for(i = 0; i < count; i++) {
			if (buf[i] != wu->w_char) {
				if (verbose) {
					unsigned long pos;

					pos = wu->w_offset +
						(unsigned long)(wu->w_len - len + i);
					fprintf(stdout, "Failure. %lu bytes "
						"into the file we expected "
						"0x%x but got 0x%x\n", pos,
						wu->w_char, buf[i]);
				}
				return 1;
			}
		}

		len -= count;
		off += count;
	}

	return 0;
}

static int vh_check_file(int fd)
{
	int ret;
	struct rb_node *node;
	struct file_chunk *chunk;

	node = rb_first(&chunk_root);
	while (node) {
		chunk = rb_entry(node, struct file_chunk, fc_node);

		ret = vh_check_chunk(&chunk->fc_write, fd);
		if (ret) {
			vh_print_extent("Verify failed", stderr,
				     &chunk->fc_write);
			return EINVAL;
		}

		node = rb_next(node);
	}

	return 0;
}

static int vh_parse_opts(int argc, char **argv, char **logname, char **fname)
{
	int c;

	while (1) {
		c = getopt(argc, argv, "v");
		if (c == -1)
			break;

		switch (c) {
		case 'v':
			verbose = 1;
			break;
		default:
			return EINVAL;
		}
	}
 
	if (argc - optind != 2)
		return EINVAL;

	*logname = argv[optind];
	*fname = argv[optind + 1];

	return 0;
}

int main(int argc, char **argv)
{
	int ret, fd;
	FILE *logfile;
	unsigned long size = 0;
	char *logname, *fname;

	ret = vh_parse_opts(argc, argv, &logname, &fname);
	if (ret) {
		vh_usage();
		return 0;
	}

	ret = vh_open_files(logname, &logfile, fname, &fd);
	if (ret)
		return 1;

	ret = vh_get_i_size(fd, &size);
	if (ret)
		return 1;

	vh_init_tree(size);

	/*
	 * Read in logfile, sorting records as we go:
	 *  - records later in the log file are assumed to overwrite those
	 *    with which they overlap.
	 */
	ret = vh_read_log(logfile);
	if (ret)
		return 1;

	/*
	 * Iterate over the file, looking for zeros where holes should
	 * be, and the right characters where holes were filled.
	 */
	ret = vh_check_file(fd);
	if (ret)
		return 1;

	return 0;
}
