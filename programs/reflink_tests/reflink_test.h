/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reflink_test.h
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


#ifndef REFLINK_TEST_H
#define REFLINK_TEST_H

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
#include <sys/time.h>
#include <sys/sem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ocfs2/ocfs2.h>
#include <ocfs2/byteorder.h>
#include "crc32table.h"

#include "aio.h"

#ifndef O_DIRECT
#define O_DIRECT		040000 /* direct disk access hint */
#endif

#define OCFS2_MAX_FILENAME_LEN	255
#define FILE_RW_FLAGS		(O_CREAT|O_RDWR)
#define FILE_RO_FLAGS		(O_RDONLY)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define HUNK_SIZE		(1024*1024)
#define DIRECTIO_SLICE		(4096)
#define PATTERN_SIZE		(1024*1024*100)
#define M_SIZE			(1024*1024)
#define G_SIZE			(1024*1024*1024)

#define BASC_TEST		0x00000001
#define RAND_TEST		0x00000002
#define BOND_TEST		0x00000004
#define STRS_TEST		0x00000008
#define MMAP_TEST		0x00000010
#define CONC_TEST		0x00000020
#define XATR_TEST		0x00000040
#define DEST_TEST		0x00000080
#define COMP_TEST		0x00000100
#define HOLE_TEST		0x00000200
#define ODCT_TEST		0x00000400
#define INLN_TEST		0x00000800
#define DSCV_TEST		0x00001000
#define VERI_TEST		0x00002000
#define PUNH_TEST		0x00004000
#define TRUC_TEST		0x00008000
#define ASIO_TEST		0x00010000

#define MPI_RET_SUCCESS		0
#define MPI_RET_FAILED		1

#define MAX_WRITE_SIZE 32768
#define RAND_CHAR_START 'A'
#define MAGIC_HOLE_CHAR (RAND_CHAR_START - 1)

#define CHUNK_SIZE	(1024*8)
#define HOSTNAME_LEN	256

struct write_unit {
	char w_char;
	unsigned long w_offset;
	unsigned int  w_len;
};

struct dest_write_unit{
	unsigned long d_chunk_no;
	unsigned long long d_timestamp;
	uint32_t d_checksum;
	char d_char;
};

struct dest_logs {
	char filename[PATH_MAX];
	unsigned long index;
};

union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};

char rand_char(void);
unsigned long get_rand(unsigned long min, unsigned long max);
int get_rand_buf(char *buf, unsigned long size);

int read_at(int fd, void *buf, size_t count, off_t offset);
int read_at_file(char *pathname, void *buf, size_t count, off_t offset);
int mmap_read_at(int fd, char *buf, size_t count, off_t offset);
int mmap_read_at_file(char *pathname, void *buf, size_t count, off_t offset);
int write_at(int fd, const void *buf, size_t count, off_t offset);
int write_at_file(char *pathname, const void *buf, size_t count, off_t offset);
int mmap_write_at(int fd, const char *buf, size_t count, off_t offset);
int mmap_write_at_file(char *pathname, const void *buf, size_t count,
		       off_t offset);

int fill_pattern(unsigned long size);
int prep_orig_file(char *file_name, unsigned long size, int once);
int prep_orig_file_dio(char *file_name, unsigned long size);
int prep_orig_file_in_chunks(char *file_name, unsigned long chunks);
int prep_orig_file_with_pattern(char *file_name, unsigned long size,
				unsigned long chunk_size, char *pattern_buf,
				int once);
int verify_pattern(char *buf, unsigned long offset, unsigned long size);
int verify_orig_file(char *orig);

int reflink(const char *oldpath, const char *newpath, unsigned long preserve);
int verify_reflink_pair(const char *src, const char *dest);
int do_reflinks(const char *src, const char *dest_prefix, unsigned long iter,
		int manner);
int do_reflinks_at_random(const char *src, const char *dest_prefix,
			  unsigned long iter);
int do_reads_on_reflinks(char *ref_pfx, unsigned long iter, unsigned long size,
			 unsigned long interval);
int do_cows_on_write(char *ref_pfx, unsigned long iter, unsigned long size,
		     unsigned long interval);
int do_cows_on_ftruncate(char *ref_pfx, unsigned long iter, unsigned long size);
int do_appends(char *ref_pfx, unsigned long iter);
int do_unlink(char *path);
int do_unlinks(char *ref_pfx, unsigned long iter);

int open_ocfs2_volume(char *device_name);

int prep_file_with_hole(char *name, unsigned long size);
FILE *open_logfile(char *logname);
int log_write(FILE *logfile, struct write_unit *wu);
void prep_rand_write_unit(struct write_unit *wu);
int do_write(int fd, struct write_unit *wu);
int do_write_file(char *fname, struct write_unit *wu);

unsigned long long get_time_microseconds(void);
void prep_rand_dest_write_unit(struct dest_write_unit *dwu,
			       unsigned long chunk_no);
int fill_chunk_pattern(char *pattern, struct dest_write_unit *dwu);
int dump_pattern(char *pattern, struct dest_write_unit *dwu);
int verify_chunk_pattern(char *pattern, struct dest_write_unit *dwu);
int do_write_chunk(int fd, struct dest_write_unit *dwu);
/*
int do_write_chunk_file(char *fname, struct dest_write_unit *du);
*/
int init_sock(char *serv, int port);
long get_verify_logs_num(char *log);
int verify_dest_file(char *log, struct dest_logs d_log, unsigned long chunk_no);
int verify_dest_files(char *log, char *orig, unsigned long chunk_no);
uint32_t crc32_checksum(uint32_t crc, char *p, size_t len);

/* Add utils for semaphore ops */
int set_semvalue(int sem_id, int val);
int semaphore_close(int sem_id);
int semaphore_p(int sem_id);
int semaphore_v(int sem_id);

int open_file(const char *filename, int flags);
int punch_hole(int fd, uint64_t start, uint64_t len);

#endif
