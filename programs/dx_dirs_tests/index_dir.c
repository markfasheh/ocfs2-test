/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * index_dir.c
 *
 * Test indexed dirs.
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

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <ocfs2/ocfs2.h>

#include <dirent.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>
#include <sys/wait.h>
#include <inttypes.h>

#define OCFS2_MAX_FILENAME_LEN          255
#define MAX_DIRENTS			40000

#define FILE_BUFFERED_RW_FLAGS  (O_CREAT|O_RDWR|O_TRUNC)
#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
                                 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

#define BASIC_TEST		0x00000001
#define RANDO_TEST		0x00000002
#define CONCU_TEST		0x00000004
#define MULTI_TEST		0x00000008
#define PRESE_TEST		0x00000010
#define BOUND_TEST		0x00000020
#define STRSS_TEST		0x00000040

struct my_dirent {
        unsigned int    type;
        unsigned int    name_len;
        unsigned int    seen;
        char            name[OCFS2_MAX_FILENAME_LEN];
};

union semun {
        int val;                    /* value for SETVAL */
        struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
        unsigned short int *array;  /* array for GETALL, SETALL */
        struct seminfo *__buf;      /* buffer for IPC_INFO */
};


static char *prog;

static char device[100];

ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;

unsigned int blocksize;
unsigned long clustersize;
unsigned int max_inline_size;

char workplace[PATH_MAX];
char dir_name[PATH_MAX];
char dir_name_prefix[OCFS2_MAX_FILENAME_LEN];

static int iteration = 1;
static int test_flags = 0x00000000;
static int random_times = 1;
static unsigned long child_nums = 2;
static unsigned long file_nums = 2;
unsigned long operated_entries = 20;
unsigned long operated_depth = 5;

pid_t *child_pid_list;

int shm_id;
int sem_id;

struct my_dirent *dirents;
unsigned int num_dirents;

char path[PATH_MAX];
char path1[PATH_MAX];

static int testno = 1;

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

unsigned int get_rand_nam(char *str, unsigned int least, unsigned most)
{
	unsigned nam_len = get_rand(least, most);
	unsigned i = 0;

	memset(str, 0, OCFS2_MAX_FILENAME_LEN);
	
	while (i < nam_len) {
		str[i] = rand_char();
		i++;
	}

	str[nam_len] = '\0';

	return nam_len;
}

int is_dot_entry(struct my_dirent *dirent)
{
	if (dirent->name_len == 1 && dirent->name[0] == '.')
		return 1;
	if (dirent->name_len == 2 && dirent->name[0] == '.'
	    && dirent->name[1] == '.')
		return 1;

	return 0;
}

int unlink_dirent(struct my_dirent *dirent)
{
	dirent->name_len = 0;
	dirent->seen = 0;
	sprintf(path1, "%s/%s", dir_name, dirent->name);

	return unlink(path1);
}

void create_and_prep_dir(void)
{
	int ret;
	struct my_dirent *dirent;
	
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
	ret = mkdir(dir_name, FILE_MODE);
	if (ret) {
	        ret = errno;
	        fprintf(stderr, "mkdir failure %d: %s\n", ret, strerror(ret));
	        exit(ret);
	}
}

void destroy_dir(void)
{
	int ret, i;
	struct my_dirent *dirent;

	for (i = 0; i < num_dirents; i++) {
		dirent = &dirents[i];

		if (dirent->name_len == 0)
			continue;

		if (!is_dot_entry(dirent)) {
			ret = unlink_dirent(dirent);
			if (ret < 0) {
				ret = errno;
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
			}

			dirent->name_len = 0;
		}

        }

	ret = rmdir(dir_name);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		exit(ret);
	}
	
	num_dirents = 0;
}

struct my_dirent *find_my_dirent(char *name)
{
	int i, len;
	struct my_dirent *my_dirent;

	len = strlen(name);

	for (i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->name_len == 0)
			continue;

		if (my_dirent->name_len == len &&
		    strcmp(my_dirent->name, name) == 0)
			return my_dirent;
	}

	return NULL;
}


void create_file(char *filename)
{
	int ret, fd;
	struct my_dirent *dirent;
	
	dirent = &dirents[num_dirents];
	num_dirents++;
	
	dirent->type = S_IFREG >> S_SHIFT;
	dirent->name_len = strlen(filename);
	dirent->seen = 0;
	strcpy(dirent->name, filename);
	
	sprintf(path, "%s/%s", dir_name, dirent->name);
	
	fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
	if (fd < 0) {
	        ret = errno;
	        fprintf(stderr, "Create file %s failure %d: %s\n", path, ret,
			strerror(ret));
	        exit(ret);
	}
	
	close(fd);
}

void create_files(char *prefix, unsigned long num)
{
        int i;
	char dirent_nam[OCFS2_MAX_FILENAME_LEN];

        for (i = 0; i < num; i++) {
		if (!prefix) {
			get_rand_nam(dirent_nam, 3, OCFS2_MAX_FILENAME_LEN/4);
			create_file(dirent_nam);
		} else {
                	snprintf(path1, PATH_MAX, "%s%011d", prefix, i);
			create_file(path1);
		}
        }
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
		if ( entries > 2)
			break;
	}

	closedir(dir);

	if (entries == 2)
		return 1;
	else
		return 0;
}

void build_dir_tree(char *dirname, unsigned long entries,
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
	}

	if (layer == 0)
		return;

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
			return;
		fd = open(fullpath, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			fprintf(stderr, "create file failure %d: %s,"
				"filename = %s\n", ret, strerror(ret),
				fullpath);
			exit(ret);
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
			return;

		build_dir_tree(fullpath, entries, layer - 1, is_random);
	}
}

void traverse_and_destroy(char *name)
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
			}
			
			ret = unlink(fullpath);
                        if (ret) {
                                ret = errno;
                                fprintf(stderr, "unlink failure %d: %s\n", ret,
                                        strerror(ret));
                                exit(ret);
                        }

		} else {
			if (dirent->d_type == S_IFDIR >> S_SHIFT) {
				if ((strcmp(dirent->d_name, ".") == 0) || 
				    (strcmp(dirent->d_name, "..") == 0)) {
					dirent = readdir(dir);
					continue;
				}
				else {
					snprintf(fullpath, PATH_MAX, "%s/%s",
						 name, dirent->d_name);

					ret = stat(fullpath, &stat_info);
					if (ret) {
						ret = errno;
						fprintf(stderr, "stat failure"
							" %d: %s\n", ret,
							strerror(ret));
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

							exit(ret);
						}
					}
					
				}
			}
		}

		dirent = readdir(dir);
	}

	ret = rmdir(name);
	if (ret ) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		exit(ret);
	}

	closedir(dir);
}

void verify_dirents(char *dir)
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
		exit(ret);
	}

	dirent = readdir(dd);
        while (dirent) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			fprintf(stderr, "Verify failure: got unexpected dirent: "
				"dirent: (ino %lu, reclen: %u, type: %u, "
				"name: %s)\n",
				dirent->d_ino, dirent->d_reclen,
				dirent->d_type, dirent->d_name);
			exit(1);
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
			exit(1);
                }

		if (my_dirent->seen) {
			fprintf(stderr, "Verify failure: duplicate dirent: "
				"(type: %u, name_len: %u, name: %s)\n",
				my_dirent->type, my_dirent->name_len,
				my_dirent->name);
                        exit(1);
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
		my_dirent= &dirents[i];
		my_dirent->seen = 0;
	}

        closedir(dd);
}

void random_unlink(int iters)
{
	int i, ret;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0; 

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		i = get_rand(0, num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		ret = unlink_dirent(dirent);
		if (ret < 0) {
			ret = errno;
			fprintf(stderr, "unlink failure %d: %s\n", ret,
				strerror(ret));
		}
		dirent->name_len = 0;

		iters--;
		times++;
	}
}


void random_fill_empty_entries(int iters)
{
	int i, ret, fd;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0; 

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
                i = get_rand(0, num_dirents - 1);
                dirent = &dirents[i];

                if (is_dot_entry(dirent))
                        continue;
                if (dirent->name_len > 0)
                        continue;

                dirent->type = S_IFREG >> S_SHIFT;
                dirent->name_len = strlen(dirent->name);
                dirent->seen = 0;

                sprintf(path, "%s/%s", dir_name, dirent->name);

                fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
                if (fd == -1) {
                        ret = errno;
                        fprintf(stderr, "create file %s failure %d: %s\n", path,
				ret, strerror(ret));
                        exit(ret);
                }

                close(fd);

                iters--;
		times++;
        }

}

void random_rename_same_reclen(int iters)
{
	int i, ret;
	struct my_dirent *dirent;
	unsigned long threshold, times = 0; 

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		i = get_rand(0, num_dirents - 1);
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;
                /*
                 * We already renamed this one
                 */
                /*
                if (dirent->name[0] == 'R')
                        continue;
                */
		strcpy(path, dirent->name);
		path[0] = 'R';
		sprintf(path1, "%s/%s", dir_name, path);
		sprintf(path, "%s/%s", dir_name, dirent->name);
		
		ret = rename(path, path1);
		if (ret) {
			ret = errno;
			fprintf(stderr, "rename failure %d: %s\n", ret,
				strerror(ret));

			fprintf(stderr, "Failed rename from %s to %s\n",
				path, path1);

			exit(ret);
		}
		dirent->name[0] = 'R';
		iters--;
		times++;
        }

}

void random_deleting_rename(int iters)
{
	int i, j, ret;
	struct my_dirent *dirent1, *dirent2;
	unsigned long threshold, times = 0; 

	threshold = iters * 2;

	while ((iters > 0) && (times < threshold)) {
		i = get_rand(0, num_dirents - 1);
		j = get_rand(0, num_dirents - 1);
		dirent1 = &dirents[i];
		dirent2 = &dirents[j];

		if (dirent1 == dirent2)
			continue;
		if (is_dot_entry(dirent1) || is_dot_entry(dirent2))
			continue;
		if (dirent1->name_len == 0 || dirent2->name_len == 0)
			continue;

		sprintf(path, "%s/%s", dir_name, dirent1->name);
		sprintf(path1, "%s/%s", dir_name, dirent2->name);

		ret = rename(path, path1);
		if (ret) {
			ret = errno;
			fprintf(stderr, "rename failure %d: %s\n", ret,
				strerror(ret));

			fprintf(stderr, "Failed rename from %s to %s\n",
				path, path1);

			exit(ret);
		}

		dirent2->type = dirent1->type;
		dirent1->name_len = 0;

		iters--;
		times++;
	}
}


static void usage(void)
{
        printf("Usage: index_dir [-i <iteration>] [-n <operated_entries>]"
	       " [-v <volume disk>] [-d depth] [-c <concurrent_process_num>]"
	       " [-m <multi_file_num>] "
               "<-w workplace> [-r <random_times>] [-f ] [-p] [-b] [-s]\n"
               "iteration specify the running times.\n"
               "operated_dir_entries specify the entires number to be "
               "operated,such as random create/unlink/rename.\n"
               "depth denotes the max depth of directory tree tested.\n"
               "concurrent_process_num specify the number of concurrent "
               "multi_file_num specify the number of multiple dirs"
               "processes to perform inline-data read/rename.\n"
               "-f to launch functional basic test.\n"
               "-b to launch boundary test.\n"
               "-r to launch random test.\n"
               "-s to launch stress test by force.\n"
               "-p to launch perserve test.\n");
        exit(1);

}


static int parse_opts(int argc, char **argv)
{
	char c;

	while (1) {
		c = getopt(argc, argv,
			   "I:i:C:c:M:m:N:n:w:W:d:sSfFpPbBD:r:R:v:V:");
		if (c == -1)
                        break;
		switch (c) {
			case 'i':
			case 'I':
				iteration = atol(optarg);
				break;
			case 'n':
			case 'N':
				operated_entries = atol(optarg);
				break;
			case 'c':
			case 'C':
				test_flags |= CONCU_TEST;
				child_nums = atol(optarg);
				break;
			case 'm':
			case 'M':
				test_flags |= MULTI_TEST;
				file_nums = atol(optarg);
				break;
			case 'w':
			case 'W':
				strcpy(workplace, optarg);
				break;
			case 'v':
			case 'V':
				strcpy(device, optarg);
				break;
			case 'd':
			case 'D':
				operated_depth = atol(optarg);
				break;
			case 'r':
			case 'R':
				test_flags |= RANDO_TEST;
				random_times = atol(optarg);
				break;
			case 'p':
			case 'P':
				test_flags |= PRESE_TEST;
				break;
			case 'f':
			case 'F':
				test_flags |= BASIC_TEST;
				break;
			case 'b':
			case 'B':
				test_flags |= BOUND_TEST;
				break;
			case 's':
			case 'S':
				test_flags = STRSS_TEST;
			default:
				break;
		}
	}
	
	if (strcmp(workplace, "") == 0)
		return EINVAL;

	if (operated_entries > MAX_DIRENTS)
		test_flags = STRSS_TEST;

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
                fprintf(stderr, "Not a ocfs2 volume!\n");
                return ret;
        }

        ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
        if (!(ocfs2_sb->s_feature_incompat &
              OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS)) {
                fprintf(stderr, "Indexed-dirs not supported"
                        " on this ocfs2 volume\n");
        }

        blocksize = 1 << ocfs2_sb->s_blocksize_bits;
        clustersize = 1 << ocfs2_sb->s_clustersize_bits;
        max_inline_size = ocfs2_max_inline_data_with_xattr(blocksize, NULL);

        return 0;
}


void setup(int argc, char *argv[])
{
	int ret;
	
	prog = strrchr(argv[0], '/');
	if (prog == NULL)
	        prog = argv[0];
	else
	        prog++;
	
	if (parse_opts(argc, argv))
	        usage();
	
	ret = open_ocfs2_volume(device);
	if (ret < 0) {
	        fprintf(stderr, "Open_ocfs2_volume failed!\n");
	        exit(ret);
	}
	
	if (test_flags & CONCU_TEST)
	        child_pid_list = (pid_t *)malloc(sizeof(pid_t) * child_nums);
	if (test_flags & MULTI_TEST)
	        child_pid_list = (pid_t *)malloc(sizeof(pid_t) * file_nums);
	
	dirents = (struct my_dirent *)malloc(sizeof(struct my_dirent) *
	                                     MAX_DIRENTS);

	memset(dirents, 0, sizeof(struct my_dirent) * MAX_DIRENTS);
	
	srand(getpid());

        snprintf(dir_name_prefix, OCFS2_MAX_FILENAME_LEN, "indexed-dirs-test");

        return;
}

void teardown(void)
{
	if (dirents)
		free(dirents);

	if (child_pid_list)
                free(child_pid_list);
}

void basic_test()
{
	printf("Test %d: Basic diectory manipulation test.\n", testno);

	create_and_prep_dir();
	create_files("testfile", operated_entries);
	verify_dirents(dir_name);
	destroy_dir();
	build_dir_tree(dir_name, operated_entries, operated_depth , 0);
	traverse_and_destroy(dir_name);
	testno++;
}

void random_test(void)
{
	int i, entries_involved;

	printf("Test %d: Random directory test.\n", testno);
	for (i = 0; i < random_times; i++) {
		printf("random iter %d:\n", i);
		entries_involved = get_rand(1, operated_entries);
        	create_and_prep_dir();
        	create_files(NULL, operated_entries);
		random_unlink(entries_involved);
		sync();
		verify_dirents(dir_name);
		
		random_fill_empty_entries(entries_involved);
		sync();
		verify_dirents(dir_name);

		exit(0);
		destroy_dir();
		
		create_and_prep_dir();
		create_files(NULL, operated_entries);
		random_rename_same_reclen(entries_involved);
		sync();
		verify_dirents(dir_name);
		destroy_dir();
		
		create_and_prep_dir();
		create_files(NULL, operated_entries);
		random_deleting_rename(entries_involved);
		sync();
		verify_dirents(dir_name);
		destroy_dir();
	}

        build_dir_tree(dir_name, operated_entries, operated_depth, 1);
        traverse_and_destroy(dir_name);

	testno++;
}

static void sigchld_handler()
{
        pid_t pid;
        int status;

        while (1) {
                pid = wait3(&status, WNOHANG, NULL);
                if (pid <= 0)
                        break;
        }
}

static void kill_all_children()
{
        int i;
        int process_nums = 0;

        if (test_flags & CONCU_TEST)
                process_nums = child_nums;

	if (test_flags & MULTI_TEST)
                process_nums = file_nums;

        for (i = 0; i < process_nums; i++)
                kill(child_pid_list[i], SIGTERM);

	free(child_pid_list);

}

static void sigint_handler()
{
        kill_all_children();

        signal(SIGINT, SIG_DFL);
        kill(getpid(), SIGINT);

}

static void sigterm_handler()
{
        kill_all_children();

        signal(SIGTERM, SIG_DFL);
        kill(getpid(), SIGTERM);

}

static int set_semvalue(void)
{
        union semun sem_union;

        sem_union.val = 1;
        if (semctl(sem_id, 0, SETVAL, sem_union) == -1) {
                perror("semctl");
                return -1;
        }

        return 0;
}

static int semaphore_p(void)
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

static int semaphore_v(void)
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

void concurrent_test(void)
{
	int ret, rc;
	int i, status;
	struct my_dirent *old_dirents;
	key_t sem_key = IPC_PRIVATE, shm_key = IPC_PRIVATE;
	
	pid_t pid;

	printf("Test %d: Concurrent directory manipulation test.\n", testno);
	create_and_prep_dir();
	create_files(NULL, operated_entries);

	/*get and init semaphore*/
	sem_id = semget(sem_key, 1, 0766 | IPC_CREAT);
	if (set_semvalue() < 0) {
	        fprintf(stderr, "Set semaphore value failed!\n");
	        exit(1);
	}
	
	/*should use shared memory here */
	old_dirents = dirents;
	dirents = NULL;

	shm_id = shmget(shm_key, sizeof(struct my_dirent) * MAX_DIRENTS,
			IPC_CREAT | 0766);
	if (shm_id < 0) {
		perror("shmget");
		exit(1);
	}

	dirents = (struct my_dirent *)shmat(shm_id, 0, 0);
	if (dirents < 0) {
		perror("shmat");
		exit(1);
	}

	shmctl(shm_id, IPC_RMID, 0);
        memmove(dirents, old_dirents, sizeof(struct my_dirent) * MAX_DIRENTS);


	/*flush out the father's i/o buffer*/
        fflush(stderr);
        fflush(stdout);

	signal(SIGCHLD, sigchld_handler);
	
	for (i = 0; i < child_nums; i++) {
                pid = fork();
                if (pid < 0) {
                        fprintf(stderr, "Fork process error!\n");
                        exit(pid);
                }
                if (pid == 0) {
                        if (semaphore_p() < 0)
                                exit(-1);
                        /*Concurrent rename for dirents*/
                        random_rename_same_reclen(operated_entries / 2);
                        if (semaphore_v() < 0)
                                exit(-1);
                        /*child exits normally*/
                        sleep(1);
                        exit(0);
                }
                if (pid > 0)
                        child_pid_list[i] = pid;
        }

	signal(SIGINT, sigint_handler);
        signal(SIGTERM, sigterm_handler);

        /*father wait all children to leave*/
        for (i = 0; i < child_nums; i++) {
                ret = waitpid(child_pid_list[i], &status, 0);
                rc = WEXITSTATUS(status);
                if (rc) {
                        fprintf(stderr, "Child %d exits abnormally with "
                                "RC=%d\n", child_pid_list[i], rc);
                        exit(rc);
                }
        }
        /*father help to verfiy dirents' consistency*/
        sleep(2);
	destroy_dir();

	/*detach shared memory*/
        if (shmdt(dirents) == -1) {
                perror("shmdt");
                 exit(1);
        }

        dirents = old_dirents;
	testno++;
}

void multi_test(void)
{
	int i, status;
	char temp_name[PATH_MAX];

        pid_t pid;
        int ret, rc;

	printf("Test %d: Multiple directories test.\n", testno);
	
	strcpy(temp_name, dir_name);
	fflush(stderr);
        fflush(stdout);

        signal(SIGCHLD, sigchld_handler);
	for (i = 0; i < file_nums; i++) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Fork process error!\n");
			exit(pid);
		}
		if (pid == 0) {
			snprintf(dir_name, OCFS2_MAX_FILENAME_LEN,
                                 "%s-%d", temp_name, getpid());
			
			create_and_prep_dir();
			create_files(NULL, operated_entries);
			random_rename_same_reclen(operated_entries / 2);
			random_unlink(operated_entries / 2);
			random_fill_empty_entries(operated_entries / 2);
			verify_dirents(dir_name);
			sync();
			sleep(2);
			destroy_dir();
			exit(0);

		}
		if (pid > 0)
			child_pid_list[i] = pid;
	}

	signal(SIGINT, sigint_handler);
        signal(SIGTERM, sigterm_handler);

        /*father wait all children to leave*/
        for (i = 0; i < file_nums; i++) {
                ret = waitpid(child_pid_list[i], &status, 0);
                rc = WEXITSTATUS(status);
                if (rc) {
                        fprintf(stderr, "Child %d exits abnormally with "
                                "RC=%d\n", child_pid_list[i], rc);
                }
        }

        testno++;
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

	if (operated_entries > almost_full_entries)
		operated_entries = almost_full_entries - 1;

	return almost_full_entries;
		
}

void get_directory_almost_full(int minus_this_many)
{
	int almost_full_entries;

	almost_full_entries = get_max_inlined_entries(max_inline_size);

	almost_full_entries -= minus_this_many;

	/* Need up to 20 characters to get a 32 byte entry */
	create_files("filename-", almost_full_entries);
}



void boundary_test(void)
{
	printf("Test %d: boundary indexed directory test.\n", testno);

	create_and_prep_dir();
	get_directory_almost_full(0);
        verify_dirents(dir_name);
	create_files("abcdefgh-", 1);
	verify_dirents(dir_name);
        destroy_dir();
        testno++;
}

void preserve_test(void)
{
	printf("Test %d: Preserving test used for space consumption.\n", testno);

        build_dir_tree(dir_name, operated_entries, operated_depth , 0);
	testno++;
}

void stress_test(void)
{
	int fd, ret;
	unsigned long i;
	char dirent_nam[OCFS2_MAX_FILENAME_LEN];

	printf("Test %d: Directory stress test.\n", testno);

	ret = mkdir(dir_name, FILE_MODE);
	if (ret) {
		ret = errno;
		fprintf(stderr, "mkdir failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}

	for (i = 0; i < operated_entries; i++) {

		get_rand_nam(dirent_nam, 1, OCFS2_MAX_FILENAME_LEN);
		sprintf(path, "%s/%s", dir_name, dirent_nam);
		fd = open(path, FILE_BUFFERED_RW_FLAGS, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			fprintf(stderr, "open failure %d: %s\n", ret,
				strerror(ret));
			exit(ret);
		}

		close(fd);

	}
	
}

void runtest(int iter)
{
	testno = 1;
	snprintf(dir_name, PATH_MAX, "%s/%s-%d", workplace,
                 dir_name_prefix, iter);
	
	if (test_flags & BASIC_TEST)
		basic_test();

	if (test_flags & RANDO_TEST)
		random_test();
	
	if (test_flags & CONCU_TEST)
		concurrent_test();
	
	if (test_flags & MULTI_TEST)
		multi_test();
	if (test_flags & BOUND_TEST)
		boundary_test();
		
	if (test_flags & PRESE_TEST)
		preserve_test();

	if (test_flags & STRSS_TEST)
		stress_test();
}

int main(int argc, char *argv[])
{
	int i;

	setup(argc, argv);

        for (i = 0; i < iteration; i++) {

                printf("################Test Round %d starts################\n", i);
		runtest(i);
                printf("################Test Round %d finish################\n", i);

        }

        teardown();

        return 0;
}
