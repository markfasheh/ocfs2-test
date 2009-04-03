/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir_ops.h
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

#ifndef DIR_OPS_H
#define DIR_OPS_H

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <ocfs2/ocfs2.h>

#include <dirent.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>
#include <sys/wait.h>
#include <inttypes.h>

#define OCFS2_MAX_FILENAME_LEN          255
#define MAX_DIRENTS                     40000

#define FILE_BUFFERED_RW_FLAGS  (O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
				 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

struct my_dirent {
	unsigned int    type;
	unsigned int    name_len;
	unsigned int    seen;
	char            name[OCFS2_MAX_FILENAME_LEN];
};

union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};

int is_dot_entry(struct my_dirent *dirent);
int unlink_dirent(char *dirname, struct my_dirent *dirent);
int unlink_dirent_nam(char *dirname, char *name);
int create_and_prep_dir(char *dirname);
int destroy_dir(char *dirname);
struct my_dirent *find_my_dirent(char *name);
int create_file(char *filename, char *dirname);
int create_files(char *prefix, unsigned long num, char *dirname);
int is_dir_empty(char *name);
int verify_dirents(char *dir);
int build_dir_tree(char *dirname, unsigned long entries, unsigned long depth,
		   int is_random);
int traverse_and_destroy(char *name);
int set_semvalue(int sem_id);
int semaphore_p(int sem_id);
int semaphore_v(int sem_id);
int get_max_inlined_entries(int max_inline_size);

#endif
