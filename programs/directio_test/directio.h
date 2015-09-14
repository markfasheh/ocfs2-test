/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * directio.h
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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
#ifndef DIRECTIO_H
#define DIRECTIO_H

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/limits.h>
#include <sys/time.h>
#include <sys/sem.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ocfs2/byteorder.h>
#include "crc32table.h"

#ifndef O_DIRECT
#define O_DIRECT		040000 /* direct disk access hint */
#endif

#define FILE_RW_FLAGS		(O_CREAT|O_RDWR)
#define FILE_RO_FLAGS		(O_RDONLY)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define CHUNK_SIZE		(1024*8)
#define DIRECTIO_SLICE		(512)

#define BASC_TEST		0x00000001
#define APPD_TEST		0x00000002
#define FIHL_TEST		0x00000004
#define DSCV_TEST		0x00000008
#define VERI_TEST		0x00000010

#define HOSTNAME_LEN	256

struct write_unit {
	unsigned long wu_chunk_no;
	unsigned long long wu_timestamp;
	uint32_t wu_checksum;
	char wu_char;
};

union log_handler {
	FILE *stream_log;
	int socket_log;
};

union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};

unsigned long get_rand_ul(unsigned long min, unsigned long max);

int open_file(const char *filename, int flags);
int get_i_size(char *filename, unsigned long *size);
int read_at(int fd, void *buf, size_t count, off_t offset);
int write_at(int fd, const void *buf, size_t count, off_t offset);

void prep_rand_dest_write_unit(struct write_unit *wu, unsigned long chunk_no);
int do_write_chunk(int fd, struct write_unit wu);
int do_read_chunk(int fd, unsigned long chunk_no, struct write_unit *wu);
int prep_orig_file_in_chunks(char *file_name, unsigned long filesize);
int verify_file(int is_remote, FILE *logfile, struct write_unit *wus,
		char *filename, unsigned long filesize);

int init_sock(char *serv, int port);
int set_semvalue(int sem_id, int val);
int semaphore_init(int val);
int semaphore_close(int sem_id);
int semaphore_p(int sem_id);
int semaphore_v(int sem_id);

int open_logfile(FILE **logfile, const char *logname);
int log_write(struct write_unit *wu, union log_handler log);
#endif
