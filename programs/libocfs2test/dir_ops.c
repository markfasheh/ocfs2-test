/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir_ops.c
 *
 * Provide generic utility fuctions on dir operations for ocfs2-tests
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
#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#include "dir_ops.h"

extern unsigned long num_dirents;
extern struct my_dirent *dirents;

int is_dot_entry(struct my_dirent *dirent)
{
	if (dirent->name_len == 1 && dirent->name[0] == '.')
		return 1;
	if (dirent->name_len == 2 && dirent->name[0] == '.'
	    && dirent->name[1] == '.')
		return 1;

	return 0;
}

int unlink_dirent(char *dirname, struct my_dirent *dirent)
{
	char path[PATH_MAX];

	sprintf(path, "%s/%s", dirname, dirent->name);

	memcpy((void *)dirent, (void *)&dirents[num_dirents - 1],
	       sizeof(struct my_dirent));
	num_dirents--;

	return unlink(path);
}

int create_and_prep_dir(char *dirname)
{
	int ret;
	struct my_dirent *dirent;

	dirents = (struct my_dirent *)malloc(sizeof(struct my_dirent) *
					     MAX_DIRENTS);
	memset(dirents, 0, sizeof(struct my_dirent) * MAX_DIRENTS);

	dirent = &dirents[0];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 1;
	strcpy(dirent->name, ".");

	dirent = &dirents[1];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 2;
	strcpy(dirent->name, "..");

	num_dirents = 2;
	ret = mkdir(dirname, FILE_MODE);
	if (ret) {
		ret = errno;
		fprintf(stderr, "mkdir failure %d: %s\n", ret, strerror(ret));
		return ret;
	}

	return 0;
}

int destroy_dir(char *dirname)
{
	unsigned long i, orig_num_dirents = num_dirents;
	int ret;
	struct my_dirent *dirent;

	for (i = 1 ; i <= orig_num_dirents; i++) {

		dirent = &dirents[orig_num_dirents - i];

		if (!is_dot_entry(dirent)) {

			ret = unlink_dirent(dirname, dirent);
			if (ret < 0) {
				ret = errno;
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
				return ret;
			}

		}

	}

	ret = rmdir(dirname);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		return ret;
	}

	num_dirents = 0;

	if (dirents)
		free(dirents);

	return 0;
}

struct my_dirent *find_my_dirent(char *name)
{
	int i, len;
	struct my_dirent *my_dirent;

	len = strlen(name);

	for (i = 0; i < num_dirents; i++) {

		my_dirent = &dirents[i];

		if (my_dirent->name_len == len &&
		    strcmp(my_dirent->name, name) == 0)
			return my_dirent;
	}

	return NULL;
}

int unlink_dirent_nam(char *dirname, char *name)
{
	struct my_dirent *dirent;

	dirent = find_my_dirent(name);

	return unlink_dirent(dirname, dirent);
}

int create_file(char *filename, char *dirname)
{
	int ret, fd;
	struct my_dirent *dirent;
	char path[PATH_MAX];

	dirent = &dirents[num_dirents];
	num_dirents++;

	dirent->type = S_IFREG >> S_SHIFT;
	dirent->name_len = strlen(filename);
	dirent->seen = 0;
	strcpy(dirent->name, filename);

	sprintf(path, "%s/%s", dirname, dirent->name);

	fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
	if (fd < 0) {
		ret = errno;
		fprintf(stderr, "Create file %s failure %d: %s\n", path, ret,
			strerror(ret));
		return ret;
	}

	close(fd);

	return 0;
}

int create_files(char *prefix, unsigned long num, char *dirname)
{
	int i, ret;
	char dirent_nam[OCFS2_MAX_FILENAME_LEN];
	char path[PATH_MAX];

	for (i = 0; i < num; i++) {
		if (!prefix) {
			get_rand_nam(dirent_nam, 3, OCFS2_MAX_FILENAME_LEN);
			ret = create_file(dirent_nam, dirname);
		} else {
			snprintf(path, PATH_MAX, "%s%011d", prefix, i);
			ret = create_file(path, dirname);
		}

		if (ret < 0)
			return ret;
	}

	return 0;
}

int is_dir_empty(char *name)
{
	DIR *dir;
	int ret, entries = 0;
	struct dirent *dirent;

	dir = opendir(name);
	if (dir < 0) {
		ret = errno;
		fprintf(stderr, "dir open failure %d: %s\n", ret,
			strerror(ret));
	}

	dirent = readdir(dir);
	while (dirent) {
		entries++;
		dirent = readdir(dir);
		if (entries > 2)
			break;
	}

	closedir(dir);

	if (entries == 2)
		return 1;
	else
		return 0;
}

int verify_dirents(char *dir)
{
	DIR *dd;
	int ret, i;
	struct dirent *dirent;
	struct my_dirent *my_dirent;

	dd = opendir(dir);
	if (!dd) {
		ret = errno;
		fprintf(stderr, "dir open failure %d: %s\n", ret,
			strerror(ret));
		return ret;
	}

	dirent = readdir(dd);
	while (dirent) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			fprintf(stderr, "Verify failure: got unexpected dirent:"
				" dirent: (ino %lu, reclen: %u, type: %u, "
				"name: %s)\n",
				dirent->d_ino, dirent->d_reclen,
				dirent->d_type, dirent->d_name);
			return -1;
		}
		if (my_dirent->type != dirent->d_type) {
			fprintf(stderr, "Verify failure: bad dirent type: "
				"memory: (type: %u, name_len: %u, name: %s), "
				"kernel: (ino %lu, reclen: %u, type: %u, "
				"name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name, dirent->d_ino,
				dirent->d_reclen, dirent->d_type,
				dirent->d_name);
			return -1;
		}

		if (my_dirent->seen) {
			fprintf(stderr, "Verify failure: duplicate dirent: "
				"(type: %u, name_len: %u, name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name);
			return -1;
		}

		my_dirent->seen++;
		dirent = readdir(dd);
	}

	for (i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->seen != 0 || my_dirent->name_len == 0)
			continue;

		if (my_dirent->seen == 0 && my_dirent->name_len != 0)
			fprintf(stderr, "Verify failure: missing dirent: "
				"(type: %u, name_len: %u, name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name);
	}

	for (i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];
		my_dirent->seen = 0;
	}

	closedir(dd);

	return 0;
}

int build_dir_tree(char *dirname, unsigned long entries,
		    unsigned long depth, int is_random)
{
	unsigned long i, dir_dirents, file_dirents;
	char fullpath[PATH_MAX];
	char dirent[OCFS2_MAX_FILENAME_LEN];
	unsigned long layer = depth;

	int fd, ret;

	ret = mkdir(dirname, FILE_MODE);
	if (ret < 0) {
		ret = errno;
		fprintf(stderr, "mkdir failure %d: %s\n", ret,
			strerror(ret));
		return ret;
	}

	if (layer == 0)
		return 0;

	if (is_random)
		dir_dirents = get_rand(1, entries - 1);
	else
		dir_dirents = entries / 2;

	file_dirents = entries - dir_dirents;

	for (i = 0; i < file_dirents; i++) {
		if (is_random) {
			get_rand_nam(dirent, 1, OCFS2_MAX_FILENAME_LEN - 20);
			snprintf(fullpath, PATH_MAX, "%s/%s%ld%ld",
				 dirname, dirent, layer, i);
		} else
			snprintf(fullpath, PATH_MAX, "%s/%s%ld%ld",
				 dirname, "F", layer, i);
		if (strlen(fullpath) > PATH_MAX)
			raise(SIGSEGV);		/* FIX ME */

		fd = open(fullpath, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			fprintf(stderr, "create file failure %d: %s,"
				"filename = %s\n", ret, strerror(ret),
				fullpath);
			return ret;
		}

		close(fd);
	}

	for (i = 0; i < dir_dirents; i++) {
		if (is_random) {
			get_rand_nam(dirent, 1, OCFS2_MAX_FILENAME_LEN - 20);
			snprintf(fullpath, PATH_MAX, "%s/%s%ld%ld",
				 dirname, dirent, layer, i);
		} else
			snprintf(fullpath, PATH_MAX, "%s/%s%ld%ld",
				 dirname, "D", layer, i);
		if (strlen(fullpath) > PATH_MAX)
			raise(SIGSEGV);		/* FIX ME */

		build_dir_tree(fullpath, entries, layer - 1, is_random);
	}

	return 0;
}

int traverse_and_destroy(char *name)
{
	DIR *dir;
	int ret;
	struct dirent *dirent;
	char fullpath[PATH_MAX];
	struct stat stat_info;

	dir = opendir(name);
	if (dir < 0) {
		ret = errno;
		fprintf(stderr, "dir open failure %d: %s\n", ret,
			strerror(ret));
		return ret;
	}

	dirent = readdir(dir);
	while (dirent) {
		if (dirent->d_type == S_IFREG >> S_SHIFT) {
			snprintf(fullpath, PATH_MAX, "%s/%s", name,
				 dirent->d_name);
			ret = stat(fullpath, &stat_info);
			if (ret) {
				ret = errno;
				fprintf(stderr, "stat failure %d: %s\n", ret,
					strerror(ret));
				return ret;
			}

			ret = unlink(fullpath);
			if (ret) {
				ret = errno;
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
				return ret;
			}

		} else {
			if (dirent->d_type == S_IFDIR >> S_SHIFT) {
				if ((strcmp(dirent->d_name, ".") == 0) ||
				    (strcmp(dirent->d_name, "..") == 0)) {
					dirent = readdir(dir);
					continue;
				} else {
					snprintf(fullpath, PATH_MAX, "%s/%s",
						 name, dirent->d_name);

					ret = stat(fullpath, &stat_info);
					if (ret) {
						ret = errno;
						fprintf(stderr, "stat failure"
							" %d: %s\n", ret,
							strerror(ret));
						return ret;
					}

					if (!is_dir_empty(fullpath))
						traverse_and_destroy(fullpath);
					else {
						ret = rmdir(fullpath);
						if (ret) {
							ret = errno;
							fprintf(stderr, "rmdir "
								"fail%d: %s\n",
								ret,
								strerror(ret));

							return ret;
						}
					}
				}
			}
		}

		dirent = readdir(dir);
	}

	ret = rmdir(name);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		exit(ret);
	}

	closedir(dir);

	return 0;
}

int set_semvalue(int sem_id)
{
	union semun sem_union;

	sem_union.val = 1;
	if (semctl(sem_id, 0, SETVAL, sem_union) == -1) {
		perror("semctl");
		return -1;
	}

	return 0;
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

int get_max_inlined_entries(int max_inline_size)
{
	unsigned int almost_full_entries;

	/*
		Borrowed from mark's inline-dirs test to measure
		how to fill up the inlined directory.
	*/
	almost_full_entries = max_inline_size / 512;
	almost_full_entries *= 512;
	almost_full_entries /= 32;

	almost_full_entries += 8;

	return almost_full_entries;
}

