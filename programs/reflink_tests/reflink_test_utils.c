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
static char chunk_pattern[CHUNK_SIZE] __attribute__ ((aligned(DIRECTIO_SLICE)));

uint32_t crc32_checksum(uint32_t crc, char *p, size_t len)
{
	const uint32_t      *b = (uint32_t *)p;
	const uint32_t      *tab = crc32table_le;

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define DO_CRC(x) crc = tab[(crc ^ (x)) & 255] ^ (crc >> 8)
#else
# define DO_CRC(x) crc = tab[((crc >> 24) ^ (x)) & 255] ^ (crc << 8)
#endif

	crc = cpu_to_le32(crc);
	/* Align it */
	if (((long)b)&3 && len) {
		do {
			uint8_t *p = (uint8_t *)b;
			DO_CRC(*p++);
			b = (void *)p;
		} while ((--len) && ((long)b)&3);
	}
	if (len >= 4) {
		/* load data 32 bits wide, xor data 32 bits wide. */
		size_t save_len = len & 3;
		len = len >> 2;
		--b; /* use pre increment below(*++b) for speed */
		do {
			crc ^= *++b;
			DO_CRC(0);
			DO_CRC(0);
			DO_CRC(0);
			DO_CRC(0);
		} while (--len);
		b++; /* point to next byte(s) */
		len = save_len;
	}
	/* And the last few bytes */
	if (len) {
		do {
			uint8_t *p = (uint8_t *)b;
			DO_CRC(*p++);
			b = (void *)p;
		} while (--len);
	}

	return le32_to_cpu(crc);
#undef DO_CRC
}

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

int fill_chunk_pattern(char *pattern, struct dest_write_unit *dwu)
{
	unsigned long mem_offset = 0;
	uint32_t checksum = 0;

	memset(pattern, 0, CHUNK_SIZE);
	mem_offset = 0;

	memmove(pattern , &dwu->d_chunk_no, sizeof(unsigned long));
	mem_offset += sizeof(unsigned long);
	memmove(pattern + mem_offset, &dwu->d_timestamp,
		sizeof(unsigned long long));
	mem_offset += sizeof(unsigned long long);
	/*
	memmove(pattern + mem_offset, &checksum, sizeof(unsigned long));
	*/
	mem_offset += sizeof(uint32_t);

	memset(pattern + mem_offset, dwu->d_char, CHUNK_SIZE - mem_offset * 2);

	checksum = crc32_checksum(~0, pattern + mem_offset,
				  (size_t)CHUNK_SIZE - mem_offset * 2);

	mem_offset = CHUNK_SIZE - mem_offset;

	memmove(pattern + mem_offset, &checksum, sizeof(uint32_t));
	mem_offset += sizeof(uint32_t);
	memmove(pattern + mem_offset, &dwu->d_timestamp,
		sizeof(unsigned long long));
	mem_offset += sizeof(unsigned long long);
	memmove(pattern + mem_offset, &dwu->d_chunk_no, sizeof(unsigned long));

	mem_offset = sizeof(unsigned long) + sizeof(unsigned long long);
	memmove(pattern + mem_offset, &checksum, sizeof(uint32_t));

	dwu->d_checksum = checksum;

	return 0;
}

int dump_pattern(char *pattern, struct dest_write_unit *dwu)
{
	unsigned long mem_offset = 0;

	memset(dwu, 0, sizeof(struct dest_write_unit));

	memmove(&dwu->d_chunk_no, pattern, sizeof(unsigned long));
	mem_offset += sizeof(unsigned long);
	memmove(&dwu->d_timestamp, pattern + mem_offset,
		sizeof(unsigned long long));
	mem_offset += sizeof(unsigned long long);
	memmove(&dwu->d_checksum, pattern + mem_offset, sizeof(uint32_t));
	mem_offset += sizeof(uint32_t);

	memmove(&dwu->d_char, pattern + mem_offset, 1);
	mem_offset = CHUNK_SIZE - mem_offset;

	memmove(&dwu->d_checksum, pattern + mem_offset, sizeof(uint32_t));
	mem_offset += sizeof(uint32_t);
	memmove(&dwu->d_timestamp, pattern + mem_offset,
		sizeof(unsigned long long));
	mem_offset += sizeof(unsigned long long);
	memmove(&dwu->d_chunk_no, pattern + mem_offset, sizeof(unsigned long));

	return 0;
}

int verify_chunk_pattern(char *pattern, struct dest_write_unit *dwu)
{
	char tmp_pattern[CHUNK_SIZE];

	fill_chunk_pattern(tmp_pattern, dwu);

	return !memcmp(pattern, tmp_pattern, CHUNK_SIZE);
}

int prep_orig_file_in_chunks(char *file_name, unsigned long chunks)
{

	int fd, ret, o_ret, flags;
	unsigned long offset = 0;
	unsigned long size = CHUNK_SIZE * chunks, chunk_no = 0;
	struct dest_write_unit dwu;

	if ((CHUNK_SIZE % DIRECTIO_SLICE) != 0) {

		fprintf(stderr, "File size in destructive tests is expected to "
			"be %d aligned, your chunk size %d is not allowed.\n",
			DIRECTIO_SLICE, CHUNK_SIZE);
		return -1;
	}

	flags = FILE_RW_FLAGS;

	fd = open64(file_name, flags, FILE_MODE);

	if (fd < 0) {
		o_ret = fd;
		fd = errno;
		fprintf(stderr, "create file %s failed:%d:%s\n", file_name, fd,
			strerror(fd));
		fd = o_ret;
		return fd;
	}

	/*
	 * Original file for desctrutive tests, it consists of chunks.
	 * Each chunks consists of following parts:
	 * chunkno + timestamp + checksum + random chars
	 * + checksum + timestamp + chunkno
	 *
	*/

	while (offset < size) {

		memset(&dwu, 0, sizeof(struct dest_write_unit));
		dwu.d_chunk_no = chunk_no;
		fill_chunk_pattern(chunk_pattern, &dwu);

		ret = write_at(fd, chunk_pattern, CHUNK_SIZE, offset);
		if (ret < 0)
			return ret;

		chunk_no++;
		offset += CHUNK_SIZE;
	}

	fsync(fd);
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
	int ret = 0, fd, o_ret;
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

unsigned long long get_time_microseconds(void)
{
	unsigned long long curtime_ms = 0;
	struct timeval curtime;

	gettimeofday(&curtime, NULL);

	curtime_ms = (unsigned long long)curtime.tv_sec * 1000000 +
					 curtime.tv_usec;

	return curtime_ms;
}

void prep_rand_dest_write_unit(struct dest_write_unit *dwu,
			       unsigned long chunk_no)
{
	char tmp_pattern[CHUNK_SIZE];

	dwu->d_char = rand_char();
	dwu->d_chunk_no = chunk_no;
	dwu->d_timestamp = get_time_microseconds();

	fill_chunk_pattern(tmp_pattern, dwu);
}

int do_write_chunk(int fd, struct dest_write_unit *dwu)
{
	int ret;
	size_t count = CHUNK_SIZE;
	off_t offset = CHUNK_SIZE * dwu->d_chunk_no;

	fill_chunk_pattern(chunk_pattern, dwu);

	ret = write_at(fd, chunk_pattern, count, offset);
	if (ret < 0)
		return ret;

	return 0;
}

int init_sock(char *serv, int port)
{
	int sockfd;
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	inet_pton(AF_INET, serv, &servaddr.sin_addr);

	connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

	return sockfd;
}

long get_verify_logs_num(char *log)
{
	FILE *logfile;
	long num_logs = 1;
	int ret;
	char arg1[100], arg2[100], arg3[100], arg4[100];

	logfile = fopen(log, "r");
	if (!logfile) {
		fprintf(stderr, "Error %d opening dest log: %s\n", errno,
			strerror(errno));
		num_logs = -1;
		goto bail;
	}

	while (!feof(logfile)) {

		ret = fscanf(logfile, "%s\t%s\t%s\t%s\n", arg1, arg2,
			     arg3, arg4);
		if (ret != 4) {
			fprintf(stderr, "input failure from dest log, ret "
				"%d, %d %s\n", ret, errno, strerror(errno));
			num_logs = -1;
			goto bail;
		}

		if (strcmp(arg1, "Reflink:"))
			continue;
		else
			num_logs++;
	}

bail:
	if (logfile)
		fclose(logfile);

	return num_logs;
}

int verify_dest_file(char *log, struct dest_logs d_log, unsigned long chunk_no)
{
	FILE *logfile;
	struct dest_write_unit *dwus, dwu;
	unsigned long i, t_bytes = sizeof(struct dest_write_unit) * chunk_no;
	unsigned long record_index = 0;
	int fd = 0, ret = 0, o_ret;
	char arg1[100], arg2[100], arg3[100], arg4[100];

	memset(&dwu, 0, sizeof(struct dest_write_unit));

	dwus = (struct dest_write_unit *)malloc(t_bytes);
	memset(dwus, 0, t_bytes);

	for (i = 0; i < chunk_no; i++)
		dwus[i].d_chunk_no = i;

	logfile = fopen(log, "r");
	if (!logfile) {
		fprintf(stderr, "Error %d opening dest log: %s\n", errno,
			strerror(errno));
		ret = -EINVAL;
		goto bail;
	}

	while (!feof(logfile)) {

		ret = fscanf(logfile, "%s\t%s\t%s\t%s\n", arg1, arg2,
			     arg3, arg4);
		if (ret != 4) {
			fprintf(stderr, "input failure from dest log, ret "
				"%d, %d %s\n", ret, errno, strerror(errno));
			ret = -EINVAL;
			goto bail;
		}

		if (!strcmp(arg1, "Reflink:"))
			continue;
		else {
			dwu.d_chunk_no = atol(arg1);
			if (dwu.d_chunk_no > chunk_no) {
				fprintf(stderr, "Chunkno grabed from logfile "
					"exceeds the filesize, you may probably"
					" specify a too small filesize.\n");
				return -1;
			}
			dwu.d_timestamp = atoll(arg2);
			dwu.d_checksum = atoi(arg3);
			dwu.d_char = arg4[0];

			record_index++;
		}

		if (dwu.d_timestamp >= dwus[dwu.d_chunk_no].d_timestamp) {

			memmove(&dwus[dwu.d_chunk_no], &dwu,
				sizeof(struct dest_write_unit));
		}

		if (record_index == d_log.index)
			break;

	}

	fd = open64(d_log.filename, open_ro_flags, FILE_MODE);
	if (fd < 0) {
		ret = fd;
		fd = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n",
			d_log.filename, fd, strerror(fd));
		goto bail;
	}

	fprintf(stdout, "Verify file %s :", d_log.filename);

	for (i = 0; i < chunk_no; i++) {

		ret = pread(fd, chunk_pattern, CHUNK_SIZE, CHUNK_SIZE * i);
		if (ret < 0) {
			o_ret = ret;
			ret = errno;
			fprintf(stderr, "read failed:%d:%s\n", ret,
				strerror(ret));
			ret = o_ret;
			goto bail;
		}

		if (ret < CHUNK_SIZE) {
			fprintf(stderr, "Short read happened, you may probably"
				" set too big filesize for verfiy_test.\n");
			return -1;
		}

		/*
		dump_pattern(chunk_pattern, &dwu);
		fprintf(stdout, "#%lu\t%llu\t%d\t%c\n", dwus[i].d_chunk_no,
			dwus[i].d_timestamp, dwus[i].d_checksum,
			dwus[i].d_char);
		*/

		/*
		dump_pattern(chunk_pattern, &dwu);
		fprintf(stdout, "#%lu\t%llu\t%d\t%c\n", dwu.d_chunk_no,
			dwu.d_timestamp, dwu.d_checksum, dwu.d_char);
		*/

		if (!verify_chunk_pattern(chunk_pattern, &dwus[i])) {

			dump_pattern(chunk_pattern, &dwu);
			fprintf(stderr, "Inconsistent chunk found in file %s!\n"
				"Expected:\tchunkno(%ld)\ttimestmp(%llu)\t"
				"chksum(%d)\tchar(%c)\nFound   :\tchunkno"
				"(%ld)\ttimestmp(%llu)\tchksum(%d)\tchar(%c)\n",
				d_log.filename,
				dwus[i].d_chunk_no, dwus[i].d_timestamp,
				dwus[i].d_checksum, dwus[i].d_char,
				dwu.d_chunk_no, dwu.d_timestamp,
				dwu.d_checksum, dwu.d_char);
			ret = -1;
			goto bail;

		}
	}

	fprintf(stdout, "Pass\n");

bail:
	if (dwus)
		free(dwus);

	if (logfile)
		fclose(logfile);

	if (fd)
		close(fd);

	return ret;
}

int verify_dest_files(char *log, char *orig, unsigned long chunk_no)
{
	unsigned long record_index = 0, log_index = 0, i;
	long log_nums;
	int ret = 0;

	struct dest_logs *logs = NULL;
	FILE *logfile = NULL;
	char arg1[100], arg2[100], arg3[100], arg4[100];

	log_nums = get_verify_logs_num(log);
	if (log_nums < 0) {
		ret = log_nums;
		goto bail;
	}

	logs = (struct dest_logs *)malloc(sizeof(struct dest_logs) * log_nums);

	strncpy(logs[0].filename, orig, PATH_MAX);

	logfile = fopen(log, "r");
	if (!logfile) {
		fprintf(stderr, "Error %d opening dest log: %s\n", errno,
			strerror(errno));
		ret = -EINVAL;
		goto bail;
	}

	while (!feof(logfile)) {

		ret = fscanf(logfile, "%s\t%s\t%s\t%s\n", arg1, arg2,
			     arg3, arg4);
		if (ret != 4) {
			fprintf(stderr, "input failure from dest log, ret "
				"%d, %d %s\n", ret, errno, strerror(errno));
			ret = -EINVAL;
			goto bail;
		}

		if (strcmp(arg1, "Reflink:")) {
			record_index++;
			continue;
		} else {
			log_index++;
			strncpy(logs[log_index].filename, arg4, PATH_MAX);
			logs[log_index].index = record_index;
			/*
			printf(" #%lu log: %s, index = %lu\n",log_index, arg4, record_index);
			*/
		}
	}

	logs[0].index = record_index;

	for (i = 0; i < log_nums; i++) {
		ret = verify_dest_file(log, logs[i], chunk_no);
		if (ret < 0) {
			ret = -1;
			goto bail;
		}
	}

bail:
	if (logfile)
		fclose(logfile);

	if (logs)

		free(logs);

	return ret;
}

int set_semvalue(int sem_id, int val)
{
	union semun sem_union;

	sem_union.val = val;
	if (semctl(sem_id, 0, SETVAL, sem_union) == -1) {
		perror("semctl");
		return -1;
	}

	return 0;
}

int semaphore_close(int sem_id)
{
	int ret = 0;

	ret = semctl(sem_id, 1, IPC_RMID);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "semctl failed, %s.\n", strerror(ret));
		return -1;
	}

	return ret;
}

int semaphore_p(int sem_id)
{
	struct sembuf sem_b;

	sem_b.sem_num = 0;
	sem_b.sem_op = -1; /* P() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(sem_id, &sem_b, 1) == -1) {
		fprintf(stderr, "semaphore_p failed\n");
		return -1;
	}

	return 0;
}

int semaphore_v(int sem_id)
{
	struct sembuf sem_b;

	sem_b.sem_num = 0;
	sem_b.sem_op = 1; /* V() */
	sem_b.sem_flg = SEM_UNDO;
	if (semop(sem_id, &sem_b, 1) == -1) {
		fprintf(stderr, "semaphore_v failed\n");
		return -1;
	}

	return 0;
}
