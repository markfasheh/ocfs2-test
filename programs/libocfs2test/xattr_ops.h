/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr_ops.h
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

#ifndef XATTR_TEST_H
#define XATTR_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAX_FILENAME_SZ         255
#define DEFAULT_XATTR_NUMS      10

#define DEFAULT_XATTR_NAME_SZ   	50
#define DEFAULT_XATTR_VALUE_SZ  	50

#define XATTR_LIST_MAX_SZ       	65536
#define XATTR_VALUE_MAX_SZ      	65536
#define XATTR_NAME_MAX_SZ       	255

#define XATTR_NAME_LEAST_SZ           	20
#define XATTR_VALUE_LEAST_SZ          	1

#define XATTR_VALUE_TO_CLUSTER		80

#define XATTR_RANDOMSIZE_UPDATE_TIMES 	20
#define XATTR_CHILD_UPDATE_TIMES      	10


enum FILE_TYPE {
	NORMAL = 1,
	DIRECTORY,
	SYMLINK
};

enum EA_NAMESPACE_CLASS {
	USER = 1,
	SYSTEM,
	TRUSTED,
	SECURITY
};

void xattr_name_generator(unsigned long xattr_no,
				 enum EA_NAMESPACE_CLASS ea_nm,
				 unsigned int from, unsigned int to);
void xattr_value_generator(int xattr_no, unsigned long from, unsigned long to);
void list_parser(char *list);
int is_namelist_member(unsigned long nu, char *name, char **name_list);
int read_ea(enum FILE_TYPE ft, int fd);
int add_or_update_ea(enum FILE_TYPE ft, int fd, int ea_flags,
		     const char *prt_str);
int remove_ea(enum FILE_TYPE ft, int fd);
int xattr_value_validator(int xattr_entry_no);
void xattr_value_constructor(int xattr_entry_no);

void xattr_setup(void);
void xattr_teardown(void);

#endif
