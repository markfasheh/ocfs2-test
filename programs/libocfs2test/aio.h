/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * aio.h
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

#ifndef AIO_H
#define AIO_H

#include <libaio.h>

struct o2test_aio {
	struct io_context *o2a_ctx;
	int o2a_nr_events;
	int o2a_cr_event;
	struct iocb **o2a_iocbs;
};

int o2test_aio_setup(struct o2test_aio *o2a, int nr_events);
int o2test_aio_pwrite(struct o2test_aio *o2a, int fd, void *buf, size_t count,
		      off_t offset);
int o2test_aio_pread(struct o2test_aio *o2a, int fd, void *buf, size_t count,
		     off_t offset);
int o2test_aio_query(struct o2test_aio *o2a, long min_nr, long nr);
int o2test_aio_destroy(struct o2test_aio *o2a);

#endif
