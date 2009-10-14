/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr_test_utils.c
 *
 * Provide generic utility fuctions for both single and multiple
 * node test.
 *
 * Written by tristan.ye@oracle.com
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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE
#include "xattr_test.h"

extern char filename[PATH_MAX];
extern unsigned long xattr_nums;
extern unsigned int xattr_name_sz;
extern unsigned long xattr_value_sz;

extern char *xattr_name;
extern char *xattr_value;
extern char *xattr_value_get;
extern char *list;
extern char **xattr_name_list_set;
extern char **xattr_name_list_get;
extern char xattr_namespace_prefix[10];

extern char value_prefix_magic[];
extern char value_postfix_magic[];
extern char value_prefix_get[20];
extern char value_postfix_get[20];
extern char value_sz[6];
extern char value_sz_get[6];
extern char *name_get;

extern open_rw_flags;
extern open_ro_flags;

void xattr_name_generator(unsigned long xattr_no,
			  enum EA_NAMESPACE_CLASS ea_nm,
			  unsigned int from,
			  unsigned int to)
{
	/*
	 * Generate a name string with given prefix,
	 * followed by a series of random characters(A-Z,a-z,0-9).
	*/
	unsigned int i;
	unsigned int seed;
	char random_ch = 'A';
	char postfix[7];

	unsigned int xattr_name_rsz;

	switch (ea_nm) {
	case USER:
		strcpy(xattr_namespace_prefix, "user");
		strcpy(xattr_name, "user.");
		break;
	case SYSTEM:
		strcpy(xattr_name, "system.");
		strcpy(xattr_namespace_prefix, "system");
		break;
	case TRUSTED:
		strcpy(xattr_name, "trusted.");
		strcpy(xattr_namespace_prefix, "trusted");
		break;
	case SECURITY:
		strcpy(xattr_name, "security.");
		strcpy(xattr_namespace_prefix, "security");
		break;
	default:
		break;
	}

	seed = time(NULL) ^ xattr_no ^ getpid();
	srandom(seed);

	xattr_name_rsz = random() % (to - from + 1) + from;
	memset(xattr_name_list_set[xattr_no], 0, xattr_name_sz + 1);

	for (i = strlen(xattr_name); i < xattr_name_rsz - 6; i++) {
		switch (i % 3) {
		case 0:
			random_ch = (xattr_no + random() + time(NULL)) % 9 + 48;
			break;
		case 1:
			random_ch = (xattr_no + random() + time(NULL)) % 26 +
				     65;
			break;
		case 2:
			random_ch = (xattr_no + random() + time(NULL)) % 26 +
				     97;
			break;
		default:
			break;
		}

		xattr_name[i] = random_ch;
	}

	xattr_name[xattr_name_rsz - 6] = 0;
	snprintf(postfix, 7, "%06ld", xattr_no);
	strcat(xattr_name, postfix);
	strcpy(xattr_name_list_set[xattr_no], xattr_name);
}

void xattr_value_generator(int xattr_no, unsigned long from,
			   unsigned long to)
{
	/*
	 * Generate a xattr value string with a series of random
	 * characters(A-Z,a-z,0-9),also with a random length.
	*/
	unsigned long i;
	char random_ch;
	unsigned int seed;
	unsigned long xattr_value_rsz;

	seed = time(NULL) ^ xattr_no ^ getpid();
	srandom(seed);

	xattr_value_rsz = random() % (to - from + 1) + from;

	for (i = 0; i < xattr_value_rsz - 1; i++) {
		switch (i % 3) {
		case 0:
			random_ch = (random() + xattr_no) % 9 + 48;
			break;
		case 1:
			random_ch = (random() + xattr_no) % 26 + 65;
			break;
		case 2:
			random_ch = (random() + xattr_no)  % 26 + 97;
			break;
		default:
			break;
		}

		xattr_value[i] = random_ch;
	}

	xattr_value[xattr_value_rsz - 1] = 0;
}

void list_parser(char *list)
{
	unsigned long list_index = 0;
	unsigned long count = 0;
	unsigned long s_index;

	while (1) {
		if (count == xattr_nums)
			break;

		memset(xattr_name_list_get[count], 0, xattr_name_sz + 1);
		s_index = 0;

		while (list[list_index]) {
			xattr_name_list_get[count][s_index] = list[list_index];
			s_index++;
			list_index++;
		}

		xattr_name_list_get[count][s_index] = '\0';
		count++;
		list_index++;
	}
}

int is_namelist_member(unsigned long nu, char *name, char **name_list)
{
	int rc = 0;
	unsigned long index = 0;

	while (index < nu) {
		if (strcmp(name, name_list[index]) == 0) {
			rc = 1;
			break;
		} else {
			index++;
		}
	}

	return rc;
}

int read_ea(enum FILE_TYPE ft, int fd)
{
	int ret = 0;

	switch (ft) {
	case NORMAL:
		ret = fgetxattr(fd, xattr_name, xattr_value_get,
				xattr_value_sz);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at fgetxattr(errno:%d, %s) "
				"on %s,xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	case SYMLINK:
		ret = lgetxattr(filename, xattr_name, xattr_value_get,
				xattr_value_sz);
		if (ret < 0) {
			fprintf(stderr, "Failed at lgetxattr(errno:%d, %s) "
				"on %s,xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	case DIRECTORY:
		ret = getxattr(filename, xattr_name, xattr_value_get,
			       xattr_value_sz);
		if (ret < 0) {
			fprintf(stderr, "Failed at getxattr(errno:%d, %s) "
				"on %s,xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	default:
		break;
	}

	return ret;
}

int add_or_update_ea(enum FILE_TYPE ft, int fd, int ea_flags,
		     const char *prt_str)
{
	int ret = 0;

	switch (ft) {
	case NORMAL:
		ret = fsetxattr(fd, xattr_name, xattr_value,
				xattr_value_sz, ea_flags);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at fsetxattr(%s,errno:%d,%s) "
				"on %s:xattr_name=%s,xattr_value_sz=%ld,"
				"xattr_value=%s\n", prt_str, ret, strerror(ret),
				filename, xattr_name, strlen(xattr_value) + 1,
				xattr_value);
			ret = -1;
		}
		break;
	case SYMLINK:
		ret = lsetxattr(filename, xattr_name, xattr_value,
				xattr_value_sz, ea_flags);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at lsetxattr(%s,errno:%d,%s) "
				"on %s:xattr_name=%s,xattr_value_sz=%ld,"
				"xattr_value=%s\n", prt_str, ret, strerror(ret),
				filename, xattr_name, strlen(xattr_value) + 1,
				xattr_value);
			ret = -1;
		}
		break;
	case DIRECTORY:
		ret = setxattr(filename, xattr_name, xattr_value,
			       xattr_value_sz, ea_flags);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at setxattr(%s,errno:%d,%s) "
				"on %s:xattr_name=%s,xattr_value_sz=%ld,"
				"xattr_value=%s\n", prt_str, ret, strerror(ret),
				filename, xattr_name, strlen(xattr_value) + 1,
				xattr_value);
			ret = -1;
		}

		break;

	default:
		break;

	}

	return ret;
}

int remove_ea(enum FILE_TYPE ft, int fd)
{
	int ret = 0;

	switch (ft) {
	case NORMAL:
		ret = fremovexattr(fd, xattr_name);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at fremovexattr(errno:%d,%s) "
				"on %s:xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	case SYMLINK:
		ret = lremovexattr(filename, xattr_name);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at lremovexattr(errno:%d,%s) "
				"on %s:xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	case DIRECTORY:
		ret = removexattr(filename, xattr_name);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "Failed at removexattr(errno:%d,%s) "
				"on %s:xattr_name=%s\n", ret, strerror(ret),
				filename, xattr_name);
			ret = -1;
		}
		break;
	default:
		break;
	}

	return ret;
}

void xattr_value_constructor(int xattr_entry_no)
{
	/*
	 * Construct a xattr value with 5 partes:
	 * Header(magic num) + xattr name + randomly generated value string +
	 * value size + Tail(magic num).
	 * It was used for verification to be designed like this.
	*/
	memset(xattr_value, 0, xattr_value_sz);
	memset(xattr_value_get, 0, xattr_value_sz);
	strcpy(xattr_name, xattr_name_list_set[xattr_entry_no]);
	memset(value_sz, 0, 6);

	xattr_value_generator(xattr_entry_no,
	xattr_value_sz -
	strlen(value_prefix_magic) -
	strlen(value_postfix_magic) -
	strlen(xattr_name) - 5,
	xattr_value_sz -
	strlen(value_prefix_magic) -
	strlen(value_postfix_magic) -
	strlen(xattr_name) - 5);

	snprintf(value_sz, 6, "%05ld", strlen(value_prefix_magic) +
		 strlen(xattr_name) + strlen(xattr_value) + 5 +
		 strlen(value_postfix_magic));

	strcpy(xattr_value_get, value_prefix_magic);
	strcat(xattr_value_get, xattr_name);
	strcat(xattr_value_get, xattr_value);
	strcat(xattr_value_get, value_sz);
	strcat(xattr_value_get, value_postfix_magic);

	memset(xattr_value, 0, xattr_value_sz);
	strcpy(xattr_value, xattr_value_get);

	return;
}
int xattr_value_validator(int xattr_entry_no)
{
	int ret = 0;
	int offset = 0;

	memset(value_prefix_get, 0, 20);
	memset(value_postfix_get, 0, 20);
	memset(value_sz_get, 0, 6);
	memset(name_get, 0, xattr_name_sz + 1);
	strcpy(xattr_name, xattr_name_list_set[xattr_entry_no]);

	memmove(value_prefix_get, xattr_value_get + offset,
		strlen(value_prefix_magic));
	value_prefix_get[strlen(value_prefix_magic)] = '\0';
	if (strcmp(value_prefix_get, value_prefix_magic) != 0) {
		fprintf(stderr, "Inconsistent Data Readed on file %s,"
			"Magic prefix conflicted!\nxattr_name=%s,"
			"xattr_value_get=%s\n", filename,
			xattr_name, xattr_value_get);
		ret = -1;
		return ret;

	}
	offset = offset + strlen(value_prefix_magic);

	memmove(name_get, xattr_value_get + offset, strlen(xattr_name));
	name_get[strlen(xattr_name)] = '\0';
	if (strcmp(name_get, xattr_name) != 0) {
		fprintf(stderr, "Inconsistent Data Readed on file %s,"
			"Name check conflicted!\nxattr_name=%s,"
			"xattr_value_get=%s\n", filename,
			xattr_name, xattr_value_get);

		ret = -1;
		return ret;
	}
	offset = strlen(xattr_value_get) - strlen(value_postfix_magic) - 5;

	memmove(value_sz_get, xattr_value_get + offset, 5);
	value_sz_get[5] = '\0';
	if (atoi(value_sz_get) != strlen(xattr_value_get)) {
		fprintf(stderr, "Inconsistent Data Readed on file %s,"
			"Value size check conflicted!\nxattr_name=%s,"
			"xattr_value_get=%s\n", filename,
			xattr_name, xattr_value_get);
		ret = -1;
		return ret;
	}
	offset = offset + 5;

	memmove(value_postfix_get, xattr_value_get + offset,
		strlen(value_postfix_magic));
	value_postfix_get[strlen(value_postfix_magic)] = '\0';
	if (strcmp(value_postfix_get, value_postfix_magic) != 0) {
		fprintf(stderr, "Inconsistent Data Readed on file %s,"
			"Magic postfix conflicted!\nxattr_name=%s,"
			"xattr_value_get=%s\n", filename,
			xattr_name, xattr_value_get);
		ret = -1;
		return ret;
	}

	return ret;
}

int verify_orig_file_xattr(enum FILE_TYPE ft, char *filename,
			   unsigned long list_size)
{
	unsigned long j;
	int ret = 0, fd;

	/*List all EA names if xattr_nums *(xattr_name_sz+1) less than 65536*/
	fd = open64(filename, open_ro_flags);

	for (j = 0; j < xattr_nums; j++)
		memset(xattr_name_list_get[j], 0, xattr_name_sz + 1);

	switch (ft) {
	case NORMAL:
		ret = flistxattr(fd, (void *)list, list_size);
		break;
	case SYMLINK:
		ret = llistxattr(filename, (void *)list, list_size);
		break;
	case DIRECTORY:
		ret = listxattr(filename, (void *)list, list_size);
		break;
	default:
		break;
	}

	list_parser(list);

	for (j = 0; j < xattr_nums; j++) {
		if (!is_namelist_member(xattr_nums, xattr_name_list_get[j],
		    xattr_name_list_set)) {
			fprintf(stderr, "Xattr list name(%s) "
				"did not match the orginal one\n",
				xattr_name_list_get[j]);
			ret = -1;
			return ret;
		}
	}

	close(fd);

	return 0;
}
