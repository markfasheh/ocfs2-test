/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file_verify.h
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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

#ifndef FILE_VERIFY_H
#define FILE_VERIFY_H

struct write_unit {
	unsigned long wu_chunk_no;
	unsigned long long wu_timestamp;
	unsigned int wu_chunksize;
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

int get_i_size(char *filename, unsigned long *size, int flags);
int prep_orig_file_in_chunks(char *file_name, unsigned long filesize,
			     unsigned int chunksize, int flags);
void prep_rand_dest_write_unit(struct write_unit *wu, unsigned long chunk_no,
			       unsigned int chunksize);
int fill_chunk_pattern(char *pattern, struct write_unit *wu);
int do_write_chunk(int fd, struct write_unit wu);
int do_read_chunk(int fd, unsigned long chunk_no, unsigned int chunksize,
		  struct write_unit *wu);
int verify_file(int is_remote, FILE *logfile, struct write_unit *wus,
		char *filename, unsigned long filesize, unsigned int chunksize,
		int verbose);

int init_sock(char *serv, int port);
int set_semvalue(int sem_id, int val);
int semaphore_init(int val);
int semaphore_close(int sem_id);
int semaphore_p(int sem_id);
int semaphore_v(int sem_id);

int open_logfile(FILE **logfile, const char *logname, int readonly);
int log_write(struct write_unit *wu, union log_handler log, int remote);

#endif
