 /* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reflink_test_utils.c
 *
 * Provide generic utility fuctions for both single and multiple
 * nodes refcount test on ocfs2.
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

#include "reflink_test.h"

extern char *orig_pattern;
extern int test_flags;

extern unsigned long page_size;
extern unsigned long file_size;

extern unsigned int blocksize;
extern unsigned long clustersize;
extern unsigned int max_inline_size;

extern int open_rw_flags;
extern int open_ro_flags;

extern ocfs2_filesys *fs;
extern struct ocfs2_super_block *ocfs2_sb;

extern char *prog;

static char buf_dio[DIRECTIO_SLICE] __attribute__ ((aligned(DIRECTIO_SLICE)));

unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

char rand_char(void)
{
	return 'A' + (char) get_rand(0, 25);
}

int read_at(int fd, void *buf, size_t count, off_t offset)
{
	int ret;
	size_t bytes_read;

	ret = pread(fd, buf, count, offset);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "read error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	bytes_read = ret;
	while (bytes_read < count) {

		ret = pread(fd, buf + bytes_read, count - bytes_read, offset +
			    bytes_read);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "read error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}

		bytes_read += ret;
	}

	return 0;
}

int read_at_file(char *pathname, void *buf, size_t count, off_t offset)
{
	int fd, ret, o_ret;

	fd  = open64(pathname, open_ro_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = read_at(fd, buf, count, offset);

	close(fd);

	return ret;
}


int mmap_read_at(int fd, char *buf, size_t count, off_t offset)
{
	int ret;
	unsigned long mmap_size = page_size;
	unsigned long size = offset + count;
	char *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_READ, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (read) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	memcpy(buf, region + offset, count);

	munmap(region, mmap_size);

	return 0;
}

int mmap_read_at_file(char *pathname, void *buf, size_t count,
		      off_t offset)
{
	int fd, ret, o_ret;

	fd  = open64(pathname, open_ro_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = mmap_read_at(fd, buf, count, offset);

	close(fd);

	return ret;
}

int write_at(int fd, const void *buf, size_t count, off_t offset)
{
	int ret;
	size_t bytes_write;

	ret = pwrite(fd, buf, count, offset);

	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "write error %d: \"%s\"\n", ret, strerror(ret));
		return -1;
	}

	bytes_write = ret;
	while (bytes_write < count) {

		ret = pwrite(fd, buf + bytes_write, count - bytes_write,
			     offset + bytes_write);

		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "write error %d: \"%s\"\n", ret,
				strerror(ret));
			return -1;
		}

		bytes_write += ret;
	}

	return 0;
}

int write_at_file(char *pathname, const void *buf, size_t count,
			 off_t offset)
{
	int fd, ret, o_ret;

	fd  = open64(pathname, open_rw_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = write_at(fd, buf, count, offset);

	close(fd);

	return ret;
}

int mmap_write_at(int fd, const char *buf, size_t count, off_t offset)
{
	int ret;
	unsigned long mmap_size = page_size;
	unsigned long size = offset + count;
	char *region;

	while (mmap_size < size)
		mmap_size += page_size;

	region = mmap(NULL, mmap_size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (region == MAP_FAILED) {
		ret = errno;
		fprintf(stderr, "mmap (write) error %d: \"%s\"\n", ret,
			strerror(ret));
		return -1;
	}

	memcpy(region + offset, buf, count);

	munmap(region, mmap_size);

	return 0;
}

int mmap_write_at_file(char *pathname, const void *buf, size_t count,
		       off_t offset)
{
	int fd, ret, o_ret;

	fd  = open64(pathname, open_rw_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", pathname, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = mmap_write_at(fd, buf, count, offset);

	close(fd);

	return ret;
}


int get_rand_buf(char *buf, unsigned long size)
{
	unsigned long i;

	for (i = 0; i < size; i++)
		buf[i] = rand_char();

	return 0;
}

int fill_pattern(unsigned long size)
{
	memset(orig_pattern, 0, PATTERN_SIZE);
	get_rand_buf(orig_pattern, size);

	return 0;
}

int prep_orig_file(char *file_name, unsigned long size, int once)
{
	int fd, fdt, ret, o_ret;
	unsigned long offset = 0, write_size = 0;
	char tmp_path[PATH_MAX];

	fd = open64(file_name, open_rw_flags, FILE_MODE);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	/*
	 * HUGEFILE large than PATTERN_SIZE will not use pattern to verify
	*/

	if (size > PATTERN_SIZE) {

		fill_pattern(PATTERN_SIZE);

		if (once) {
			while (offset < size) {
				if ((offset + PATTERN_SIZE) > size)
					write_size = size - offset;
				else
					write_size = PATTERN_SIZE;

				ret = write_at(fd, orig_pattern, write_size,
					       offset);
				if (ret < 0)
					return ret;

				offset += write_size;
			}

		} else {

			snprintf(tmp_path, PATH_MAX, "%s-tmp-file", file_name);
			fdt = open64(tmp_path, open_rw_flags, FILE_MODE);

			unlink(tmp_path);

			while (offset < size) {

				if ((offset + M_SIZE) > size)
					write_size = size - offset;
				else
					write_size = M_SIZE;

				ret = write_at(fd, orig_pattern +
					       offset % PATTERN_SIZE,
					       write_size,
					       offset);
				if (ret < 0)
					return ret;

				ret = write_at(fdt, orig_pattern +
					       offset % PATTERN_SIZE,
					       write_size,
					       offset);
				if (ret < 0)
					return ret;

				offset += write_size;
			}

			close(fdt);
		}

		close(fd);
		return 0;
	}

	/*
	* file within PATTERN_SIZE should use pattern to verify
	*/

	fill_pattern(size);

	if (once) {
		ret = write_at(fd, orig_pattern, size, 0);
		if (ret < 0)
			return ret;

		close(fd);
		return 0;
	}

	/*
	* otherwise we need a tmp file to make extents incontiguous by
	* writing data by turns
	*/

	snprintf(tmp_path, PATH_MAX, "%s-tmp-file", file_name);
	fdt = open64(tmp_path, open_rw_flags, FILE_MODE);
	if (fdt < 0) {
		o_ret = fdt;
		fdt = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", tmp_path, fdt,
			strerror(fdt));
		fdt = o_ret;
		return fdt;
	}

	unlink(tmp_path);

	while (offset < size) {
		if (test_flags & RAND_TEST)
			write_size = (size >= HUNK_SIZE * 2) ?
				      get_rand(1, HUNK_SIZE * 2) :
				      get_rand(1, size);
		else
			write_size = HUNK_SIZE;

		if (offset + write_size > size)
			write_size = size - offset;

		ret = write_at(fd, orig_pattern + offset, write_size, offset);
		if (ret < 0)
			return ret;

		ret = write_at(fdt, orig_pattern + offset, write_size, offset);
		if (ret < 0)
			return ret;

		offset += write_size;
	}

	close(fdt);
	close(fd);

	return 0;
}

int prep_orig_file_dio(char *file_name, unsigned long size)
{
	int fd, ret, o_ret, flags;
	unsigned long offset = 0, write_size = DIRECTIO_SLICE;


	if ((size % DIRECTIO_SLICE) != 0) {

		fprintf(stderr, "File size in directio tests is expected to "
			"be %d aligned, your size %ld is not allowed.\n",
			DIRECTIO_SLICE, size);
		return -1;
	}

	flags = FILE_RW_FLAGS | O_DIRECT;

	fd = open64(file_name, flags, FILE_MODE);

	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}


	while (offset < size) {

		if (offset + write_size > size)
			write_size = size - offset;

		memset(buf_dio, rand_char(), DIRECTIO_SLICE);

		ret = pwrite(fd, buf_dio, write_size, offset);
		if (ret < 0) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "write failed:%d:%s\n", ret,
				strerror(ret));
			return ret;
		}

		offset += write_size;

	}

	close(fd);
	return 0;
}

int verify_reflink_pair(const char *src, const char *dest)
{
	int fds, fdd, ret, o_ret;
	char bufs[HUNK_SIZE], bufd[HUNK_SIZE];
	unsigned long reads, readd;

	fds = open64(src, open_ro_flags);
	if (fds < 0) {
		o_ret = fds;
		fds = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", src, fds,
			strerror(fds));
		fds = o_ret;
		return fds;
	}

	fdd = open64(dest, open_ro_flags);
	if (fdd < 0) {
		o_ret = fdd;
		fdd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", src, fdd,
			strerror(fdd));
		fdd = o_ret;
		return fdd;
	}

	while ((reads = read(fds, bufs, HUNK_SIZE)) &&
	       (readd = read(fdd, bufd, HUNK_SIZE))) {

		if (reads != readd) {
			fprintf(stderr, "data readed are not in the "
				"same size\n");
			return 1;
		}

		ret = memcmp(bufs, bufd, reads);
		if (ret) {
			fprintf(stderr, "data readed are different\n");
			return ret;
		}
	}

	close(fds);
	close(fdd);

	return 0;
}

int verify_pattern(char *buf, unsigned long offset, unsigned long size)
{
	unsigned long i;

	char *start = orig_pattern + offset;

	for (i = 0; i < size; i++) {
		if (buf[i] != start[i]) {
			fprintf(stderr, "original file verify failed at byte: "
				"%ld\n", i);
			return -1;
		}
	}

	return 0;
}


int verify_orig_file(char *orig)
{
	int ret, fd, o_ret;
	unsigned long readed, offset = 0;
	unsigned long verify_size = 0;
	char buf[HUNK_SIZE];

	unsigned int mmap_size = page_size;
	void *region;

	fd = open64(orig, open_ro_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", orig, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	if (file_size > PATTERN_SIZE)
		verify_size = PATTERN_SIZE;
	else
		verify_size = file_size;

	/*
	* use mmap read to verify original file
	*/
	if (test_flags & MMAP_TEST) {

		while (mmap_size < verify_size)
			mmap_size += page_size;

		region = mmap(NULL, mmap_size, PROT_READ, MAP_SHARED, fd, 0);
		if (region == MAP_FAILED) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "mmap (read) error %d: \"%s\"\n", ret,
				strerror(ret));
			ret = o_ret;
			return ret;
		}

		ret = verify_pattern(region, 0, verify_size);
		if (ret) {
			fprintf(stderr, "Verify orig file by mmap failed\n");
			return ret;
		}

		munmap(region, mmap_size);
		return 0;

	}

	while ((readed = read(fd, buf, HUNK_SIZE)) > 0) {
		ret = verify_pattern(buf, offset, readed);
		if (ret) {
			fprintf(stderr, "Verify original file failed\n");
			return ret;
		}

		offset += readed;
		if (offset >= PATTERN_SIZE)
			break;
	}

	close(fd);

	return 0;
}

int do_reflinks(const char *src, const char *dest_prefix,
		       unsigned long iter, int manner)
{
	int ret;
	unsigned long i, j = 0;
	char from[PATH_MAX], to[PATH_MAX];

	for (i = 0; i < iter; i++) {

		snprintf(to, PATH_MAX, "%sr%ld", dest_prefix, i);
		if (i == 0) {
			strcpy(from, src);
		} else {
			if (manner == 0)
				j = 0;
			if (manner == 1)
				j = i - 1;
			if (manner > 1)
				j = get_rand(0, i - 1);

			if (j == 0)
				strcpy(from, src);
			else
				snprintf(from, PATH_MAX, "%sr%ld",
					 dest_prefix, j);
		}

		ret = reflink(from, to, 1);
		if (ret) {
			fprintf(stderr, "do_reflinks failed\n");
			return ret;
		}

	}

	return 0;
}

int do_reflinks_at_random(const char *src, const char *dest_prefix,
				 unsigned long iter)
{
	int ret, o_ret, fd, method;
	unsigned long i = 0;
	char dest[PATH_MAX], buf[2 * HUNK_SIZE], *ptr;

	unsigned long write_size = 0, append_size = 0, truncate_size = 0;
	unsigned long read_size = 0, offset = 0;
	unsigned long tmp_file_size = file_size;

	fd = open64(src, open_rw_flags | O_APPEND);

	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n",
			src, fd, strerror(fd));
		fd = o_ret;
		return fd;
	}

	if (test_flags & ODCT_TEST)
		ptr = buf_dio;
	else
		ptr = buf;

	while (i < iter) {

		method = get_rand(1, 4);

		if (method == 1) {
			if (test_flags & ODCT_TEST)
				write_size = DIRECTIO_SLICE;
			else
				write_size = get_rand(1, 2 * HUNK_SIZE);

			get_rand_buf(ptr, write_size);

			if (test_flags & ODCT_TEST)
				offset = get_rand(0, file_size /
						  DIRECTIO_SLICE) *
						  DIRECTIO_SLICE;
			else
				offset = get_rand(0, file_size - 1);

			ret = write_at(fd, ptr, write_size, offset);
			if (ret < 0) {
				fprintf(stderr, "do_reflinks_at_random failed"
					" at write_at\n");
				return ret;
			}
		}

		if (method == 2) {
			if (test_flags & ODCT_TEST)
				append_size = DIRECTIO_SLICE;
			else
				append_size = get_rand(1, 2 * HUNK_SIZE);
			get_rand_buf(ptr, append_size);
			ret = write(fd, ptr, append_size);
			if (ret < 0) {
				o_ret = ret;
				ret = errno;
				fprintf(stderr, "do_reflinks_at_random failed "
					"at appending on file %s:%d:%s.\n",
					src, ret, strerror(ret));
				ret = o_ret;
				return ret;
			}

			tmp_file_size += append_size;
		}
		if (method == 3) {
			if (test_flags & ODCT_TEST)
				truncate_size = get_rand(1, file_size /
							 DIRECTIO_SLICE) *
							 DIRECTIO_SLICE;
			else
				truncate_size = get_rand(1, file_size);
			ret = ftruncate(fd, truncate_size);
			if (ret < 0) {
				o_ret = ret;
				ret = errno;
				fprintf(stderr, "do_reflinks_at_random failed "
					"at truncating on file %s:%d:%s.\n",
					src, ret, strerror(ret));
				ret = o_ret;
				return ret;
			}

			tmp_file_size = truncate_size;
		}
		if (method == 4) {

			if (test_flags & ODCT_TEST)
				read_size = DIRECTIO_SLICE;
			else
				read_size = get_rand(1, 2 * HUNK_SIZE);

			if (test_flags & ODCT_TEST)
				offset = get_rand(0, tmp_file_size /
						  DIRECTIO_SLICE) *
						  DIRECTIO_SLICE;
			else
				offset = get_rand(0, tmp_file_size);

			if ((offset + read_size) > tmp_file_size)
				read_size = tmp_file_size - offset;

			if (test_flags & ODCT_TEST)

				read_size = read_size / DIRECTIO_SLICE *
					    DIRECTIO_SLICE;

			ret = read_at(fd, ptr, read_size, offset);
			if (ret < 0) {
				o_ret = ret;
				ret = errno;
				fprintf(stderr, "do_reflinks_at_random failed "
					"at reading on file %s:%d:%s.\n",
					src, ret, strerror(ret));
				ret = o_ret;
				return ret;
			}
		}

		snprintf(dest, PATH_MAX, "%sr%ld", dest_prefix, i);
		ret = reflink(src, dest, 1);
		if (ret) {
			fprintf(stderr, "do_reflinks_at_random failed at "
				"reflink(%ld) after method(%d) operation.\n",
				i, method);
			return ret;
		}

		i++;
	}

	close(fd);

	return 0;
}

int do_reads_on_reflinks(char *ref_pfx, unsigned long iter,
				unsigned long size, unsigned long interval)
{
	int ret, fd, o_ret;
	unsigned long i, read_size, offset = 0;
	char ref_path[PATH_MAX];
	char buf[HUNK_SIZE * 2], *ptr;

	if (test_flags & ODCT_TEST)
		ptr = buf_dio;
	else
		ptr = buf;

	for (i = 0; i < iter; i++) {
		if ((i % 3) == 0)
			continue;

		snprintf(ref_path, PATH_MAX, "%sr%ld", ref_pfx, i);
		fd = open64(ref_path, open_ro_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				ref_path, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}

		offset = 0;

		while (offset < size) {
			if (test_flags & RAND_TEST)
				read_size = ((i % 2) == 0) ?
					     get_rand(1, HUNK_SIZE) :
					     get_rand(HUNK_SIZE, HUNK_SIZE * 2);
			else if (test_flags & ODCT_TEST)
				read_size = DIRECTIO_SLICE;
			else
				read_size = HUNK_SIZE;

			memset(ptr, 0, read_size);

			if (test_flags & MMAP_TEST) {

				/*
				if ((offset + read_size) > size)
					break;
				*/

				if ((offset + read_size) > size)
					read_size = size - offset;

				ret = mmap_read_at(fd, ptr, read_size, offset);
				if (ret) {
					fprintf(stderr, "mmap_read_at fail\n");
					return ret;
				}

			} else {

				/*
				if ((offset + read_size) > size)
					break;
				*/

				if ((offset + read_size) > size)
					read_size = size - offset;

				ret = read_at(fd, ptr, read_size, offset);
				if (ret) {

					fprintf(stderr, "read_at failed\n");
					return ret;
				}
			}

			if (test_flags & RAND_TEST)
				offset = offset + read_size +
					 get_rand(0, interval);
			else
				offset = offset + read_size + interval;
		}

		close(fd);
	}

	return 0;
}

int do_cows_on_write(char *ref_pfx, unsigned long iter,
			    unsigned long size, unsigned long interval)
{
	int ret, fd, o_ret;
	unsigned long i, write_size, offset = 0;
	char ref_path[PATH_MAX];
	char buf[HUNK_SIZE * 2], *ptr;

	if (test_flags & ODCT_TEST)
		ptr = buf_dio;
	else
		ptr = buf;

	for (i = 0; i < iter; i++) {
		/*
		 * leave 1/3 reflinks still shared the same extents
		*/
		if ((i % 3) == 0)
			continue;

		snprintf(ref_path, PATH_MAX, "%sr%ld", ref_pfx, i);
		fd = open64(ref_path, open_rw_flags);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "open file %s failed:%d:%s\n",
				ref_path, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}

		offset = 0;

		while (offset < size) {
			if (test_flags & RAND_TEST)
				write_size = ((i % 2) == 0) ?
					     get_rand(1, HUNK_SIZE) :
					     get_rand(HUNK_SIZE, HUNK_SIZE * 2);
			else if (test_flags & ODCT_TEST)
				write_size = DIRECTIO_SLICE;
			else
				write_size = HUNK_SIZE;

			get_rand_buf(ptr, write_size);

			if (test_flags & MMAP_TEST) {

				/*
				if ((offset + write_size) > size)
					break;
				*/

				if ((offset + write_size) > size)
					write_size = size - offset;

				ret = mmap_write_at(fd, ptr, write_size,
						    offset);
				if (ret) {

					fprintf(stderr, "mmap_write_at fail\n");
					return ret;
				}

			} else {

				/*
				if ((offset + write_size) > size)
					break;
				*/

				if ((offset + write_size) > size)
					write_size = size - offset;

				ret = write_at(fd, ptr, write_size, offset);
				if (ret) {

					fprintf(stderr, "write_at failed\n");
					return ret;
				}
			}

			if (test_flags & RAND_TEST)
				offset = offset + write_size +
					 get_rand(0, interval);
			else
				offset = offset + write_size + interval;
		}

		close(fd);
	}

	return 0;
}

int do_cows_on_ftruncate(char *ref_pfx, unsigned long iter,
				unsigned long size)
{
	int ret, fd, o_ret;
	unsigned long i, truncate_size;
	char ref_path[PATH_MAX];

	for (i = 0; i < iter; i++) {
		snprintf(ref_path, PATH_MAX, "%sr%ld", ref_pfx, i);
		fd = open64(ref_path, open_rw_flags);
		if (fd < 0) {
			fd = errno;
			fprintf(stderr, "create file %s failed:%d:%s\n",
				ref_path, fd, strerror(fd));
			return fd;
		}

		if (test_flags & RAND_TEST)
			truncate_size = get_rand(0, size);
		else if (test_flags & ODCT_TEST)
			truncate_size = ((size / DIRECTIO_SLICE) / iter) * i *
					DIRECTIO_SLICE;
		else
			truncate_size = (size / iter) * i;

		ret = ftruncate(fd, truncate_size);
		if (ret) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "truncate file %s failed:%d:%s\n",
				ref_path, ret, strerror(ret));
			ret = o_ret;
			return ret;
		}

		close(fd);
	}

	return 0;
}

int do_appends(char *ref_pfx, unsigned long iter)
{
	int ret, fd, o_ret;
	unsigned long i, append_size;
	char ref_path[PATH_MAX];
	char buf[HUNK_SIZE], *ptr;

	if (test_flags & ODCT_TEST)
		ptr = buf_dio;
	else
		ptr = buf;

	for (i = 0; i < iter; i++) {
		snprintf(ref_path, PATH_MAX, "%sr%ld", ref_pfx, i);
		fd = open64(ref_path, open_rw_flags | O_APPEND);
		if (fd < 0) {
			o_ret = fd;
			fd = errno;
			fprintf(stderr, "create file %s failed:%d:%s\n",
				ref_path, fd, strerror(fd));
			fd = o_ret;
			return fd;
		}

		if (test_flags & RAND_TEST)
			append_size = get_rand(1, HUNK_SIZE);
		else if (test_flags & ODCT_TEST)
			append_size = DIRECTIO_SLICE;
		else
			append_size = HUNK_SIZE;

		get_rand_buf(ptr, append_size);

		ret = write(fd, ptr, append_size);
		if (ret < 0) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "append file %s failed:%d:%s\n",
				ref_path, ret, strerror(ret));
			ret = o_ret;
			return ret;
		}

		close(fd);
	}

	return 0;
}

int do_unlink(char *path)
{
	int ret, o_ret;

	ret = unlink(path);
	if (ret < 0) {
		o_ret = ret;
		ret = errno;
		fprintf(stderr, "unlink file %s failed:%d:%s\n",
			path, ret, strerror(ret));
		ret = o_ret;
		return ret;
	}

	return 0;
}

int do_unlinks(char *ref_pfx, unsigned long iter)
{
	int ret, o_ret;

	unsigned long i;
	char ref_path[PATH_MAX];

	for (i = 0; i < iter; i++) {
		snprintf(ref_path, PATH_MAX, "%sr%ld", ref_pfx, i);
		ret = unlink(ref_path);
		if (ret < 0) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "unlink file %s failed:%d:%s\n",
				ref_path, ret, strerror(ret));
			ret = o_ret;
			return ret;
		}
	}

	return 0;
}

int open_ocfs2_volume(char *device_name)
{
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;
	int ret;
	uint64_t superblock = 0, block_size = 0;

	ret = ocfs2_open(device_name, open_flags, superblock, block_size,
			 &fs);
	if (ret < 0) {
		com_err(prog, ret,
			"while opening file system for reflink_test.");
		return ret;
	}

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
	if (!(ocfs2_sb->s_feature_incompat &
	      OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE)) {
		fprintf(stderr, "refcount is not supported"
			" on this ocfs2 volume\n");
		return -1;
	}

	blocksize = 1 << ocfs2_sb->s_blocksize_bits;
	clustersize = 1 << ocfs2_sb->s_clustersize_bits;
	max_inline_size = ocfs2_max_inline_data(blocksize);

	ocfs2_close(fs);

	return 0;
}

/*
 * Following funcs borrowed from fill_verify_holes
 * to test holes punching and filling in reflinks
 *
*/

int prep_file_with_hole(char *name, unsigned long size)
{
	int ret, fd;
	char trailer[1];

	fd = open64(name, open_rw_flags | O_TRUNC | O_APPEND, FILE_MODE);

	if (fd < 0) {
		fprintf(stderr, "open error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	ret = ftruncate(fd, size - 1);
	if (ret < 0) {

		close(fd);
		fprintf(stderr, "ftruncate error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	memset(trailer, 0, 1);

	ret = write(fd, trailer, 1);

	if (ret < 0) {

		close(fd);
		fprintf(stderr, "write error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	close(fd);

	return 0;

}

FILE *open_logfile(char *logname)
{
	FILE *logfile;

	if (!logname)
		logfile = stdout;
	else
		logfile = fopen(logname, "wa");

	if (!logfile) {
		fprintf(stderr, "Error %d creating logfile: %s\n", errno,
			strerror(errno));
		return NULL;
	}

	return logfile;
}

int log_write(FILE *logfile, struct write_unit *wu)
{
	int fd;

	fprintf(logfile, "%c\t%lu\t%u\n", wu->w_char, wu->w_offset, wu->w_len);

	fflush(logfile);

	fd = fileno(logfile);

	fsync(fd);

	return 0;
}

void prep_rand_write_unit(struct write_unit *wu)
{
again:
	wu->w_char = RAND_CHAR_START + (char) get_rand(0, 52);
	wu->w_offset = get_rand(0, file_size - 1);
	wu->w_len = (unsigned int) get_rand(1, MAX_WRITE_SIZE);

	if (wu->w_offset + wu->w_len > file_size)
		wu->w_len = file_size - wu->w_offset;

	/* sometimes the random number might work out like this */
	if (wu->w_len == 0)
		goto again;

	assert(wu->w_char >= RAND_CHAR_START && wu->w_char <= 'z');
	assert(wu->w_len <= MAX_WRITE_SIZE);
	assert(wu->w_len > 0);
}

int do_write(int fd, struct write_unit *wu)
{
	int ret;
	char buf[MAX_WRITE_SIZE];
	void *mapped;

	if (test_flags & MMAP_TEST) {

		mapped = mmap(0, file_size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (mapped == MAP_FAILED) {

			fprintf(stderr, "mmap error %d: \"%s\"\n", errno,
				strerror(errno));
			return -1;
		}

		memset(mapped + wu->w_offset, wu->w_char, wu->w_len);

		munmap(mapped, file_size);

		return 0;
	}

	memset(buf, wu->w_char, wu->w_len);
	ret = pwrite(fd, buf, wu->w_len, wu->w_offset);
	if (ret == -1) {
		fprintf(stderr, "write error %d: \"%s\"\n", errno,
			strerror(errno));
		return -1;
	}

	return 0;
}

int do_write_file(char *fname, struct write_unit *wu)
{
	int fd, ret, o_ret;

	fd  = open64(fname, open_rw_flags);
	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", fname, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	ret = do_write(fd, wu);

	close(fd);

	return ret;
}
