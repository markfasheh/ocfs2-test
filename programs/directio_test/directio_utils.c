 /* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * directio_utils.c
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

#include "directio.h"

static char chunk_pattern[CHUNK_SIZE] __attribute__ ((aligned(DIRECTIO_SLICE)));

extern int open_rw_flags;
extern int open_ro_flags;

extern int test_flags;
extern int verbose;

static uint32_t crc32_checksum(uint32_t crc, char *p, size_t len)
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

unsigned long get_rand_ul(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + (rand() % (max - min + 1));
}

static char rand_char(void)
{
	return 'A' + (char) get_rand_ul(0, 25);
}

int open_file(const char *filename, int flags)
{
	int fd, ret = 0;

	fd = open64(filename, flags, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "open file %s failed:%d:%s\n", filename, ret,
			strerror(ret));
		return -1;
	}

	return fd;
}

int get_i_size(char *filename, unsigned long *size)
{
	struct stat stat;
	int ret = 0, fd;

	fd = open_file(filename, open_ro_flags);
	if (fd)
		return fd;

	ret = fstat(fd, &stat);
	if (ret == -1) {
		ret = errno;
		fprintf(stderr, "stat failure %d: %s\n", ret, strerror(ret));
		return ret;
	}

	*size = (unsigned long) stat.st_size;
	return ret;
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

	return count;
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

	return count;
}

static int fill_chunk_pattern(char *pattern, struct write_unit *wu)
{
	unsigned long offset = 0;
	uint32_t checksum = 0;

	memset(pattern, 0, CHUNK_SIZE);
	offset = 0;

	memmove(pattern , &wu->wu_chunk_no, sizeof(unsigned long));
	offset += sizeof(unsigned long);
	memmove(pattern + offset, &wu->wu_timestamp, sizeof(unsigned long long));
	offset += sizeof(unsigned long long);

	offset += sizeof(uint32_t);

	memset(pattern + offset, wu->wu_char, CHUNK_SIZE - offset * 2);

	checksum = crc32_checksum(~0, pattern + offset,
				  (size_t)CHUNK_SIZE - offset * 2);

	offset = CHUNK_SIZE - offset;

	memmove(pattern + offset, &checksum, sizeof(uint32_t));
	offset += sizeof(uint32_t);
	memmove(pattern + offset, &wu->wu_timestamp,
		sizeof(unsigned long long));
	offset += sizeof(unsigned long long);
	memmove(pattern + offset, &wu->wu_chunk_no, sizeof(unsigned long));

	offset = sizeof(unsigned long) + sizeof(unsigned long long);
	memmove(pattern + offset, &checksum, sizeof(uint32_t));

	wu->wu_checksum = checksum;

	return 0;
}

static int dump_pattern(char *pattern, struct write_unit *wu)
{
	unsigned long offset = 0;

	memset(wu, 0, sizeof(struct write_unit));

	memmove(&wu->wu_chunk_no, pattern, sizeof(unsigned long));
	offset += sizeof(unsigned long);
	memmove(&wu->wu_timestamp, pattern + offset, sizeof(unsigned long long));
	offset += sizeof(unsigned long long);
	memmove(&wu->wu_checksum, pattern + offset, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	memmove(&wu->wu_char, pattern + offset, 1);
	offset = CHUNK_SIZE - offset;

	memmove(&wu->wu_checksum, pattern + offset, sizeof(uint32_t));
	offset += sizeof(uint32_t);
	memmove(&wu->wu_timestamp, pattern + offset, sizeof(unsigned long long));
	offset += sizeof(unsigned long long);
	memmove(&wu->wu_chunk_no, pattern + offset, sizeof(unsigned long));

	return 0;
}

static int verify_chunk_pattern(char *pattern, struct write_unit *wu)
{
	char tmp_pattern[CHUNK_SIZE];

	fill_chunk_pattern(tmp_pattern, wu);

	return !memcmp(pattern, tmp_pattern, CHUNK_SIZE);
}

static unsigned long long get_time_microseconds(void)
{
	unsigned long long curtime_ms = 0;
	struct timeval curtime;

	gettimeofday(&curtime, NULL);

	curtime_ms = (unsigned long long)curtime.tv_sec * 1000000 +
					 curtime.tv_usec;
	return curtime_ms;
}

void prep_rand_dest_write_unit(struct write_unit *wu, unsigned long chunk_no)
{
	char tmp_pattern[CHUNK_SIZE];

	wu->wu_char = rand_char();
	wu->wu_chunk_no = chunk_no;
	wu->wu_timestamp = get_time_microseconds();

	fill_chunk_pattern(tmp_pattern, wu);
}

int do_write_chunk(int fd, struct write_unit wu)
{
	int ret;
	size_t count = CHUNK_SIZE;
	off_t offset = CHUNK_SIZE * wu.wu_chunk_no;

	fill_chunk_pattern(chunk_pattern, &wu);

	ret = write_at(fd, chunk_pattern, count, offset);
	if (ret < 0)
		return ret;

	return ret;
}

int do_read_chunk(int fd, unsigned long chunk_no, struct write_unit *wu)
{
	int ret;
	size_t count = CHUNK_SIZE;
	off_t offset = CHUNK_SIZE * chunk_no;

	ret = read_at(fd, chunk_pattern, count, offset);
	if (ret < 0)
		return ret;

	dump_pattern(chunk_pattern, wu);

	return ret;
}

int prep_orig_file_in_chunks(char *file_name, unsigned long filesize)
{

	int fd, ret, flags;
	unsigned long offset = 0, chunk_no = 0;
	static struct write_unit wu;

	if ((CHUNK_SIZE % DIRECTIO_SLICE) != 0) {

		fprintf(stderr, "chunk size in destructive tests is expected to "
			"be %d aligned, your CHUNK_SIZE %d is not allowed.\n",
			DIRECTIO_SLICE, CHUNK_SIZE);
		return -EINVAL;
	}

	flags = FILE_RW_FLAGS;

	fd = open_file(file_name, flags);
	if (fd < 0)
		return fd;

	/*
	 * Original file for desctrutive tests, it consists of chunks.
	 * Each chunks consists of following parts:
	 * chunkno + timestamp + checksum + random chars
	 * + checksum + timestamp + chunkno
	 *
	*/
	while (offset < filesize) {

		prep_rand_dest_write_unit(&wu, chunk_no);

		fill_chunk_pattern(chunk_pattern, &wu);

		ret = do_write_chunk(fd, wu);
		if (ret < 0)
			return ret;

		chunk_no++;
		offset += CHUNK_SIZE;
	}

	close(fd);

	return 0;
}

int verify_file(int is_remote, FILE *logfile, struct write_unit *remote_wus,
		char *filename, unsigned long filesize)
{
	int fd = 0, ret = 0;
	struct write_unit *wus, wu, ewu;
	unsigned long num_chunks = filesize / CHUNK_SIZE;
	unsigned long i, t_bytes = sizeof(struct write_unit) * num_chunks;
	char arg1[100], arg2[100], arg3[100], arg4[100];

	memset(&wu, 0, sizeof(struct write_unit));
	memset(&ewu, 0, sizeof(struct write_unit));

	wus = (struct write_unit *)malloc(t_bytes);
	memset(wus, 0, t_bytes);

	if (is_remote) {
		memcpy(wus, remote_wus, t_bytes);
		goto verify_body;
	}

	for (i = 0; i < num_chunks; i++)
		wus[i].wu_chunk_no = i;

	while (!feof(logfile)) {

		ret = fscanf(logfile, "%s\t%s\t%s\t%s\n", arg1, arg2,
			     arg3, arg4);
		if (ret != 4) {
			fprintf(stderr, "input failure from write log, ret "
				"%d, %d %s\n", ret, errno, strerror(errno));
			ret = -EINVAL;
			goto bail;
		}

		wu.wu_chunk_no = atol(arg1);
		if (wu.wu_chunk_no > num_chunks) {
			fprintf(stderr, "Chunkno grabed from write log"
				"exceeds the filesize, you may probably"
				" specify a too small filesize.\n");
			return -EINVAL;
		}

		wu.wu_timestamp = atoll(arg2);
		wu.wu_checksum = atoi(arg3);
		wu.wu_char = arg4[0];

		if (wu.wu_timestamp >= wus[wu.wu_chunk_no].wu_timestamp) {

			memmove(&wus[wu.wu_chunk_no], &wu,
				sizeof(struct write_unit));
		}
	}

verify_body:
	fd = open_file(filename, open_ro_flags);
	if (fd < 0)
		return fd;

	for (i = 0; i < num_chunks; i++) {
		/*
		 * Verification consists of two following parts:
		 *
		 *    - verify write records.
		 *    - verify pattern of chunks absent from write records.
		 */
		
		ret = do_read_chunk(fd, i, &wu);
		if (ret < 0)
			return ret;
		/*
		 * verify pattern of chunks absent from write records.
		 */
		if (!wus[i].wu_timestamp) {

			if (verbose)
				fprintf(stdout, "  verifying #%lu chunk "
					"out of write records\n", i);
			/*
			 * skip holes
			 */
			if (!wu.wu_timestamp)
				continue;

			if (wu.wu_chunk_no != i) {
				fprintf(stderr, "Chunk no expected: %lu, Found: %lu\n",
					i, wu.wu_chunk_no);
				return -EINVAL;
			}

			/*
			 * recalculate checksum
			 */
			memcpy(&ewu, &wu, sizeof(wu));
                	fill_chunk_pattern(chunk_pattern, &ewu);
                	if (wu.wu_checksum != ewu.wu_checksum) {
                	        fprintf(stderr, "Checksum expected: %u Found: %u\n",
                	                ewu.wu_checksum, wu.wu_checksum);
                	        return -1;
                	}

			continue;
		}

		/*
		 * verify write records in logfile.
		 */
		if (verbose)
			fprintf(stdout, "  verifying #%lu chunk in write "
				"records\n", i);

		if (ret < CHUNK_SIZE) {
			fprintf(stderr, "Short read(readed:%d, expected:%d)"
				"happened, you may probably set too big "
				"filesize for verfiy_test.\n", ret, CHUNK_SIZE);
			return -1;
		}

		fill_chunk_pattern(chunk_pattern, &wu);

		if (!verify_chunk_pattern(chunk_pattern, &wus[i])) {

			dump_pattern(chunk_pattern, &wu);
			fprintf(stderr, "Inconsistent chunk found in file %s!\n"
				"Expected:\tchunkno(%ld)\ttimestmp(%llu)\t"
				"chksum(%d)\tchar(%c)\nFound   :\tchunkno"
				"(%ld)\ttimestmp(%llu)\tchksum(%d)\tchar(%c)\n",
				filename,
				wus[i].wu_chunk_no, wus[i].wu_timestamp,
				wus[i].wu_checksum, wus[i].wu_char,
				wu.wu_chunk_no, wu.wu_timestamp,
				wu.wu_checksum, wu.wu_char);
			ret = -1;
			goto bail;

		}
	}

	ret = 0;

bail:
	if (wus)
		free(wus);

	if (fd)
		close(fd);
	
	return ret;
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

int semaphore_init(int val)
{
	int ret, sem_id;
	key_t sem_key = IPC_PRIVATE;

	/*get and init semaphore*/
	sem_id = semget(sem_key, 1, 0766 | IPC_CREAT);
	if (sem_id < 0) {
		sem_id = errno;
		fprintf(stderr, "semget failed, %s.\n", strerror(sem_id));
		return -1;
	}

	ret = set_semvalue(sem_id, 1);
	if (ret < 0) {
		fprintf(stderr, "Set semaphore value failed!\n");
		return ret;
	}

	return sem_id;
}

int semaphore_close(int sem_id)
{
	int ret = 0;

	ret = semctl(sem_id, 0, IPC_RMID);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "semctl to close sem failed, %s.\n", strerror(ret));
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

int open_logfile(FILE **logfile, const char *logname)
{
	if (test_flags & VERI_TEST) {
		if (!logname)
			*logfile = stdin;
		else
			*logfile = fopen(logname, "r");
	} else {
		if (!logname)
			*logfile = stdout;
		else *logfile = fopen(logname, "wa");
		
	}

	if (!(*logfile)) {
		fprintf(stderr, "Error %d opening logfile: %s\n", errno,
			strerror(errno));
		return -EINVAL;
	}

	return 0;
}

int log_write(struct write_unit *wu, union log_handler log)
{
	int fd, ret = 0;
	char log_rec[1024];

	if (test_flags & DSCV_TEST) {
		snprintf(log_rec, sizeof(log_rec), "%lu\t%llu\t%d\t%c\n",
			 wu->wu_chunk_no, wu->wu_timestamp, wu->wu_checksum,
			 wu->wu_char);
		ret = write(log.socket_log, log_rec, strlen(log_rec) + 1);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "write socket error:%d, %s\n",
				ret , strerror(ret));
			return -EINVAL;
		}
	} else {
		fprintf(log.stream_log, "%lu\t%llu\t%d\t%c\n", wu->wu_chunk_no,
			wu->wu_timestamp, wu->wu_checksum, wu->wu_char);
                fflush(log.stream_log);
		fd = fileno(log.stream_log);
		fsync(fd);
	}

	return ret;
}
