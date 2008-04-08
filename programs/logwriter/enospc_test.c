/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define MB 1048576
#define HDR_SIZE 512
#define MAX_FILE_SIZE 2147483648
#define FILE_NAME_SIZE 512
#define TEST_FILE_SIZE 524288000
#define OCFS2_SUPER_MAGIC 0x7461636f
#define OCFS1_SUPER_MAGIC 0xa156f7eb

char *buf, *hdr;

unsigned long create_file(char *file_name, unsigned long file_size, 
						int write_to_file, int delete)
{
	struct stat fst;	
	int ret = 0, fd, err;
	int64_t offset, fsz_bytes, bw;

	printf("now creating file %s, size %luMB\n", file_name, file_size/MB);
	fd = open(file_name, O_CREAT, S_IRWXU|S_IRWXG);
	if (fd < 0) {
		printf("error %d, creating file1 %s\n", errno, file_name);
		return errno;
	}	
	close(fd);

        fd = open(file_name, O_RDWR|O_LARGEFILE|O_DIRECT, S_IRWXU|S_IRWXG);
	if (fd < 0) {
		printf("error %d, creating file2 %s\n", errno, file_name);
		return errno;
	}	

	fsz_bytes = HDR_SIZE;
	bw = pwrite(fd, hdr, fsz_bytes, 0);
	if (bw < 1) {
		printf("error %d, writing to file header, bytes written %ld\n", 
								errno, bw);
		goto exit2;
	}

	offset = fsz_bytes;
	fsz_bytes = file_size - fsz_bytes;
	ret = ftruncate(fd, fsz_bytes);
	if (ret < 0) {
		err = errno;
		if (fstat(fd, &fst) < 0) {
			printf("fstat errored %d \n", errno);
			goto exit2;
		}

		printf("error %d, truncating file to size %ld, current size " 
				"is %lu\n", err, fsz_bytes, fst.st_size);
		goto exit2;
	}

	bw = MB;
	if (!write_to_file)
		goto exit1;
	while (offset < file_size) {
		bw = pwrite(fd, buf, MB, offset);
		if (bw < 0) {
			printf("error %d, during write\n", errno);
			goto exit1;
		}
		offset += MB;
	}
	goto exit1;
exit2:
	if (fstat(fd, &fst) < 0) {
		printf("fstat errored %d \n", errno);
		goto exit1;
	}

	printf("file %s, size requested %lu, file size %lu failed with "
		"error %d\n", file_name, file_size, fst.st_size, errno);

	if (!delete) {
		printf("NOT deleting file .... \n");
		goto exit1;
	}
		printf("deleting file ... ");

	if (!unlink(file_name))
		printf("file %s deleted\n", file_name);
	else
		printf("delete failed with error %d\n", errno);
exit1:
	if (fd) close(fd);
	return ret;
}
		
int main(int argc, char *argv[])
{
	struct statfs fs_stat;
	char *dirname, *file_name, *file_ext;
	int file_name_len, start_fileno;
	unsigned long  numfiles, fill_size, vol_size, free_size, file_size;

	int ret=0;
	if ((argc != 2) || (strcmp(argv[1],"-h") == 0)) {
		printf("USAGE: enospc_test </path/to/ocfs2/volume> \n");
		return 0;
	}

        dirname = memalign(512, FILE_NAME_SIZE);
	memcpy(dirname, argv[1], strlen(argv[1]));

	ret=statfs(dirname, &fs_stat);
	if (ret < 0) {
		printf("statfs failed with error %d\n", errno);
		return 0;
	}

	if ((fs_stat.f_type != OCFS2_SUPER_MAGIC) &&
			(fs_stat.f_type != OCFS1_SUPER_MAGIC)) {
		printf("not an OCFS2 filesystem (magic 0x%ld)\n", 
							fs_stat.f_type);
		return 0;
	}

	vol_size = fs_stat.f_bsize * fs_stat.f_blocks;
	if (vol_size < TEST_FILE_SIZE) {
		printf("OCFS2 volume too small %luMB\n", vol_size/MB);
		return 0;
	}

	/* 
	** need to fill fileystem with files until the free size is 
	** TEST_FILE_SIZE 
	*/
	free_size = fs_stat.f_bavail * fs_stat.f_bsize;
        if (free_size < TEST_FILE_SIZE) {
                printf("volume has not enough free space(required free "
		"size %lu, free size %luMB)\n", 
		(unsigned long)(TEST_FILE_SIZE/MB), (free_size/MB));
                return 0;
        }
	fill_size = free_size - TEST_FILE_SIZE;
	numfiles = fill_size/MAX_FILE_SIZE + 1;		
	file_size = (unsigned long)MAX_FILE_SIZE;
	if (file_size > fill_size)
		file_size = fill_size;

        file_name = memalign(512, FILE_NAME_SIZE);
        file_ext = memalign(512, FILE_NAME_SIZE);
        hdr = memalign(512, HDR_SIZE);
        buf = memalign(4096, MB);
        if (!file_name || !file_ext || !buf) {
		printf("Error %d allocating space for file_name || "
							"file_ext\n", errno);
		return 0;
	}

	memset(file_name, 0, FILE_NAME_SIZE);
	memcpy(file_name, dirname, strlen(dirname));
	memcpy(&file_name[strlen(dirname)], "/testfile", strlen("/testfile"));

	printf("\n\nvolume size %lu\n", vol_size/MB);
	printf("space occupied %luMB\n", (vol_size - free_size)/MB);
	printf("free space %luMB\n", free_size/MB);
	printf("number of files used to fill %lu\n", numfiles);
	printf("each filesize %luMB\n\n\n", file_size/MB);
	file_name_len = strlen(file_name);
	start_fileno=1;
	while (numfiles-- > 0) {
		create_file(file_name, file_size, 0, 0);
		if (ret < 0) {
			printf("file creation failed during prepare stage "
						"with errno %d\n", errno);
			return 0;
		}
		free_size -= file_size;
		if (free_size < MAX_FILE_SIZE) {
			file_size = free_size - TEST_FILE_SIZE - 100 * MB; 
		}
		sprintf(file_ext, "%d", start_fileno++);
		memcpy(&file_name[file_name_len], file_ext, strlen(file_ext));
		if (file_size < 0) 
			break;
		sync();
		sync();
	}
	create_file(file_name, (TEST_FILE_SIZE - 100 * MB), 0, 0);
	create_file(file_name, (unsigned long)((MAX_FILE_SIZE -1) * 2), 1, 1);
	sleep(2);
	create_file(file_name, (TEST_FILE_SIZE - 100 * MB), 1, 0);
	return 0;
}
