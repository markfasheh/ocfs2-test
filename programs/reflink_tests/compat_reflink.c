/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * compat_reflink.c
 *
 * Use ioctl() based reflink call for old kernels which have no
 * reflink(2) system call implemented.
 *
 * Written by tristan.ye@oracle.com
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ocfs2/ocfs2.h>

extern int open_ro_flags;

int reflink(const char *oldpath, const char *newpath, unsigned long preserve)
{
	int fd, ret, o_ret;
	struct reflink_arguments args;

	args.old_path = (__u64)oldpath;
	args.new_path = (__u64)newpath;
	args.preserve = preserve;

	fd = open64(oldpath, open_ro_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", oldpath, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = ioctl(fd, OCFS2_IOC_REFLINK, &args);
	if (ret) {
		o_ret = ret;
		ret = errno;
		fprintf(stderr, "ioctl failed:%d:%s\n", ret, strerror(ret));
		close(fd);
		ret = o_ret;
		return ret;
	}

	close(fd);

	return 0;
}
