/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * aio.c
 *
 * Provide generic utility fuctions on aio operations for ocfs2-tests
 *
 * Written by tristan.ye@oracle.com
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

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/ioctl.h>

#include "aio.h"

int o2test_aio_setup(struct o2test_aio *o2a, int nr_events)
{
	int ret = 0, i;

	o2a->o2a_ctx = NULL;
	o2a->o2a_nr_events = nr_events;
	o2a->o2a_iocbs = (struct iocb **)malloc(sizeof(struct iocb *) *
						nr_events);

	for (i = 0; i < nr_events; i++)
		o2a->o2a_iocbs[i] = (struct iocb *)malloc(sizeof(struct iocb));

	ret = io_setup(nr_events, &(o2a->o2a_ctx));
	if (ret) {
		ret = errno;
		fprintf(stderr, "error %s during %s\n", strerror(errno),
			"io_setup");
		ret = -1;
	}

	o2a->o2a_cr_event = 0;

	return ret;
}

int o2test_aio_pwrite(struct o2test_aio *o2a, int fd, void *buf, size_t count,
		      off_t offset)
{
	int ret = 0;
	struct iocb *iocbs[] = { o2a->o2a_iocbs[o2a->o2a_cr_event] };

	if (o2a->o2a_cr_event >= o2a->o2a_nr_events) {
		fprintf(stderr, "current aio context didn't support %d "
			"requests\n", o2a->o2a_cr_event);
		return -1;
	}

	io_prep_pwrite(o2a->o2a_iocbs[o2a->o2a_cr_event], fd, buf,
		       count, offset);
	ret = io_submit(o2a->o2a_ctx, 1, iocbs);
	if (ret != 1) {
		ret = errno;
		fprintf(stderr, "error %s during %s\n", strerror(errno),
			"io_submit");
		ret = -1;
	}

	o2a->o2a_cr_event++;

	return ret;
}

int o2test_aio_pread(struct o2test_aio *o2a, int fd, void *buf, size_t count,
		     off_t offset)
{
	int ret = 0;
	struct iocb *iocbs[] = { o2a->o2a_iocbs[o2a->o2a_cr_event] };

	if (o2a->o2a_cr_event >= o2a->o2a_nr_events) {
		fprintf(stderr, "current aio context didn't support %d "
			"requests\n", o2a->o2a_cr_event);
		return -1;
	}

	io_prep_pread(o2a->o2a_iocbs[o2a->o2a_cr_event], fd, buf,
		      count, offset);
	ret = io_submit(o2a->o2a_ctx, 1, iocbs);
	if (ret != 1) {
		ret = errno;
		fprintf(stderr, "error %s during %s\n", strerror(errno),
			"io_submit");
		ret = -1;
	}

	o2a->o2a_cr_event++;

	return ret;
}

int o2test_aio_query(struct o2test_aio *o2a, long min_nr, long nr)
{
	int ret = 0;
	struct io_event ev;

	ret = io_getevents(o2a->o2a_ctx, min_nr, nr, &ev, NULL);
	if (ret < min_nr) {
		ret = errno;
		fprintf(stderr, "error %s during %s\n", strerror(errno),
			"io_getevents");
		ret = -1;
	}

	return ret;
}

int o2test_aio_destroy(struct o2test_aio *o2a)
{
	int ret = 0, i;

	ret = io_destroy(o2a->o2a_ctx);
	if (ret) {
		ret = errno;
		fprintf(stderr, "error %s during %s\n", strerror(errno),
			"io_destroy");
		ret = -1;
	}

	for (i = 0; i < o2a->o2a_nr_events; i++)
		if (o2a->o2a_iocbs[i])
			free(o2a->o2a_iocbs[i]);

	if (o2a->o2a_iocbs)
		free(o2a->o2a_iocbs);

	o2a->o2a_ctx = NULL;
	o2a->o2a_nr_events = 0;
	o2a->o2a_cr_event = 0;

	return ret;
}
