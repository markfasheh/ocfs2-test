/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr-test.c
 *
 * xattr testing binary for single node,it includes functionality,
 * stress,concurrent,and multiple files test by specifying various
 * arguments.
 *
 * Written by tristan.ye@oracle.com
 *
 * XXX: This could easily be turned into an mpi program.
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

#include "xattr-test.h"

static char *prog;
static char path[PATH_SZ + 1];
char filename[MAX_FILENAME_SZ + 1];

static unsigned long iter_nums = DEFAULT_ITER_NUMS;
unsigned long xattr_nums = DEFAULT_XATTR_NUMS;

unsigned int xattr_name_sz = DEFAULT_XATTR_NAME_SZ;
unsigned long xattr_value_sz = DEFAULT_XATTR_VALUE_SZ;

char *xattr_name;
char *xattr_value;
char *xattr_value_get;
char *list;
static unsigned long list_sz;
char **xattr_name_list_set;
char **xattr_name_list_get;
char xattr_namespace_prefix[10];
static char file_type[10];

pid_t *child_pid_list;

static enum EA_NAMESPACE_CLASS ea_nm_class = USER;
static enum FILE_TYPE ea_filetype = NORMAL;

static int do_list = 1;
static int do_random_test;
static int keep_ea;
static int do_multi_process_test;
static int child_nums;
static int do_multiple_file_test;
static int file_nums;

static int testno = 1;

char value_prefix_magic[] = "abcdefghijklmnopqrs";
char value_postfix_magic[] = "srqponmlkjihgfedcba";
char value_prefix_get[20];
char value_postfix_get[20];
char value_sz[6];
char value_sz_get[6];
char *name_get;

static void usage(void)
{
	printf("usage: %s [-i <iterations>] [-x <EA_nums>] [-n <EA_namespace>] "
	       "[-t <File_type>] [-l <EA_name_length>] [-s <EA_value_size>] "
	       "[-m <Child_nums>] [-f <file_nums> ] [-r] [-k] <path>.\n\n"
	       "<iterations> defaults to %d.\n"
	       "<EA_nums> defaults to %d.\n"
	       "<EA_namespace> defaults to user,currently,can be user,system,"
	       "trusted and security.\n"
	       "<EA_name_length> defaults to %d,more than %d,less than %d.\n"
	       "<EA_value_size> defaults to %d,more than %d,less than %d.\n"
	       "<Child_nums> specify the number of child process to "
	       "launch concurrent operations.\n"
	       "<file_nums> represents the number of file we expects to be "
	       "operated simultaneously by multiple processes.\n"
	       "<File_type> defaults to common file,can be normal,"
	       "directory and symlink.\n"
	       "[-r] launch the random update/add/remove test.\n"
	       "[-k] keep the EA entries after test.\n"
	       "<path> is required.\n"
	       "Will rotate up to <iterations> times.\n"
	       "In each pass, will create a series of files,"
	       "symlinks and directories,in the directory.\n"
	       "which <path> specifies,"
	       "then do vairous operations specified file.\n\n", prog ,
	       DEFAULT_ITER_NUMS, DEFAULT_XATTR_NUMS, DEFAULT_XATTR_NAME_SZ,
	       XATTR_NAME_LEAST_SZ, XATTR_NAME_MAX_SZ, DEFAULT_XATTR_VALUE_SZ,
	       XATTR_VALUE_LEAST_SZ, XATTR_VALUE_MAX_SZ);

	exit(1);
}

static int parse_opts(int argc, char **argv);

static void setup(int argc, char *argv[])
{
	unsigned long i;

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	if (do_multi_process_test == 1) {
		if (xattr_value_sz < xattr_name_sz + 50) {
			fprintf(stderr, "Please Specify a xattr_value_sz more "
				"than xattr_name_sz + 50,when you try to do "
				"concurrent test with multiple processes!\n");
			exit(1);
		}
	}

	xattr_name = (char *)malloc(xattr_name_sz + 1);
	name_get = (char *)malloc(xattr_name_sz + 1);
	xattr_value = (char *)malloc(xattr_value_sz);
	xattr_value_get = (char *)malloc(xattr_value_sz);
	xattr_name_list_set = (char **)malloc(sizeof(char *) * xattr_nums);

	if (do_multi_process_test == 1)
		child_pid_list = (pid_t *)malloc(sizeof(pid_t) * child_nums);
	if (do_multiple_file_test == 1)
		child_pid_list = (pid_t *)malloc(sizeof(pid_t) * file_nums);

	for (i = 0; i < xattr_nums; i++)
		xattr_name_list_set[i] = (char *)malloc(xattr_name_sz + 1);

	list_sz = (unsigned long)((xattr_name_sz + 1) * xattr_nums);
	if (list_sz > XATTR_LIST_MAX_SZ) {
		do_list = 0;
		fprintf(stderr, "Warning:list size exceed,due to "
			"(xattr_name_sz+1)*xattr_nums was greater than 65536,");
		fprintf(stderr, "will not launch list test!\n");
		fflush(stderr);

	} else {
		list = (char *)malloc(list_sz);
		xattr_name_list_get = (char **)malloc(sizeof(char *) *
						      xattr_nums);
		for (i = 0; i < xattr_nums; i++)
			xattr_name_list_get[i] = (char *)malloc(xattr_name_sz
								+ 1);
	}

	return;
}

static void teardown(void)
{
	unsigned long j;

	free((void *)xattr_name);
	free((void *)name_get);
	free((void *)xattr_value);
	free((void *)xattr_value_get);

	for (j = 0; j < xattr_nums; j++)
		free((void *)xattr_name_list_set[j]);

	free((void *)xattr_name_list_set);

	if (do_list) {
		free((void *)list);

		for (j = 0; j < xattr_nums; j++)
			free((void *)xattr_name_list_get[j]);

		free((void *)xattr_name_list_get);

	}

	return;
}

static int parse_opts(int argc, char **argv)
{
	int c;

	while (1) {
		c = getopt(argc, argv,
			   "i:x:I:X:n:N:l:L:s:S:n:N:kRt:KrT:M:m:F:f:");
		if (c == -1)
			break;

		switch (c) {
		case 'i':
		case 'I':
			iter_nums = atol(optarg);
			break;
		case 'x':
		case 'X':
			xattr_nums = atol(optarg);
			break;
		case 'n':
		case 'N':
			if (strcmp(optarg, "user") == 0) {
				ea_nm_class = USER;
				break;
			}
			if (strcmp(optarg, "system") == 0) {
				ea_nm_class = SYSTEM;
				break;
			}
			if (strcmp(optarg, "trusted") == 0) {
				ea_nm_class = TRUSTED;
				break;
			}
			if (strcmp(optarg, "security") == 0) {
				ea_nm_class = SECURITY;
				break;
			}
			return EINVAL;
		case 't':
		case 'T':
			strcpy(file_type, optarg);
			if (strcmp(optarg, "normal") == 0) {
				ea_filetype = NORMAL;
				break;
			}
			if (strcmp(optarg, "directory") == 0) {
				ea_filetype = DIRECTORY;
				break;
			}
			if (strcmp(optarg, "symlink") == 0) {
				ea_filetype = SYMLINK;
				break;
			}
			return EINVAL;
		case 'l':
		case 'L':
			xattr_name_sz = atol(optarg);
			break;
		case 's':
		case 'S':
			xattr_value_sz = atol(optarg);
			break;
		case 'r':
		case 'R':
			do_random_test = 1;
			break;
		case 'k':
		case 'K':
			keep_ea = 1;
			break;
		case 'm':
		case 'M':
			do_multi_process_test = 1;
			child_nums = atol(optarg);
			break;
		case 'f':
		case 'F':
			do_multiple_file_test = 1;
			file_nums = atol(optarg);
			break;
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 1)
		return EINVAL;
#ifdef DO_LIMIT
	if ((xattr_name_sz > 255) || (xattr_name_sz < 11))
		return EINVAL;

	if (xattr_value_sz > 65536)
		return EINVAL;
#endif

	strcpy(path, argv[optind]);
	if (path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';

	return 0;
}

static void judge_sys_return(int ret, const char *sys_func)
{
	if (ret < 0) {
		perror(sys_func);
		teardown();
		exit(1);
	} else
		return;
}

static void sigchld_handler()
{
	pid_t	pid;
	union wait status;
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

	if (do_multi_process_test == 1)
		process_nums = child_nums;
	if (do_multiple_file_test == 1)
		process_nums = file_nums;

	for (i = 0; i < process_nums; i++)
		kill(child_pid_list[i], SIGTERM);

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

static void atexit_hook(void)
{
	int i;
	int process_nums = 0;

	if (do_multi_process_test == 1)
		process_nums = child_nums;
	if (do_multiple_file_test == 1)
		process_nums = file_nums;

	for (i = 0; i < process_nums; i++)
		kill(child_pid_list[i], SIGKILL);

	free(child_pid_list);
}

static void one_round_run(enum FILE_TYPE ft, int round_no)
{
	int fd = 0, ret, status;
	pid_t pid;
	unsigned long j;
	int i, k;
	char *write_buf = NULL;

	testno = 1;
	/* Launch multiple file test by forking multiple processes,each process
	   manipulates a file*/
	if (do_multiple_file_test == 1) {

		printf("Test %d: Doing Xattr multiple file test.\n", testno);
		fflush(stdout);
		fflush(stderr);
		signal(SIGCHLD, sigchld_handler);
		for (i = 0; i < file_nums; i++) {
			pid = fork();
			if (pid < 0) {
				fprintf(stderr, "Fork process error!\n");
				fflush(stderr);
				teardown();
				exit(1);
			}
			/*child try to create/modify file,add/update xattr*/
			if (pid == 0) {
				memset(filename, 0, MAX_FILENAME_SZ + 1);
				snprintf(filename, MAX_FILENAME_SZ,
					"%s/multiplefile_%s_test_round%d-%d-%d",
					path, file_type, round_no, i, getpid());

				switch (ft) {
				case NORMAL:
					fd = open(filename,
						  FILE_FLAGS_CREATE, FILE_MODE);
					judge_sys_return(fd, "open");
					break;
				case SYMLINK:
					ret = symlink("/no/such/file",
						      filename);
					judge_sys_return(ret, "symlink");
					break;
				case DIRECTORY:
					ret = mkdir(filename, FILE_MODE);
					judge_sys_return(ret, "mkdir");
					break;
				default:
					break;
				}

				for (j = 0; j < xattr_nums; j++) {
					/*add,update xatr*/
					memset(xattr_name, 0,
					       xattr_name_sz + 1);
					memset(xattr_value, 0, xattr_value_sz);
					memset(xattr_value_get, 0,
					       xattr_value_sz);
					if (do_random_test == 1)
						xattr_name_generator(j,
								     ea_nm_class,
								     XATTR_NAME_LEAST_SZ,
								     xattr_name_sz);
					else
						xattr_name_generator(j,
								     ea_nm_class,
								     xattr_name_sz,
								     xattr_name_sz);
					xattr_value_constructor(j);
					ret = add_or_update_ea(ft, fd,
							       XATTR_CREATE,
							       "add");
					if (ret < 0) {
						teardown();
						exit(1);
					}
					xattr_value_constructor(j);
					ret = add_or_update_ea(ft, fd,
							       XATTR_REPLACE,
							       "update");
					if (ret < 0) {
						teardown();
						exit(1);
					}
					ret = read_ea(ft, fd);
					if (ret < 0) {
						teardown();
						exit(1);
					}
					ret = xattr_value_validator(j);
					if (ret < 0) {
						teardown();
						exit(1);
					}
					/*append file content*/
					if (ft == NORMAL) {
						ftruncate(fd, 0);
						fsync(fd);
						write_buf = realloc(write_buf,
								    CLUSTER_SIZE);
						memset(write_buf, 'a'+j%26,
						       CLUSTER_SIZE);
						ret = pwrite(fd, write_buf,
							    CLUSTER_SIZE, 0);
						judge_sys_return(ret, "write");
					}
				}
				free(write_buf);
				close(fd);
				exit(0);

			}
			if (pid > 0)
				child_pid_list[i] = pid;
		}

		/*fater*/
		signal(SIGINT, sigint_handler);
		signal(SIGTERM, sigterm_handler);
		atexit(atexit_hook);
		for (i = 0; i < file_nums; i++)
			ret = waitpid(child_pid_list[i], &status, 0);
		
		testno++;

		return;
	}

	memset(filename, 0, MAX_FILENAME_SZ + 1);
	if (do_list)
		memset(list, 0, list_sz);
	snprintf(filename, MAX_FILENAME_SZ, "%s/test_%s-%d",
		 path, file_type, round_no);

	switch (ft) {
	case NORMAL:
		fd = open(filename, FILE_FLAGS_CREATE, FILE_MODE);
		judge_sys_return(fd, "open");
		break;
	case SYMLINK:
		ret = symlink("/no/such/file", filename);
		judge_sys_return(ret, "symlink");
		break;
	case DIRECTORY:
		ret = mkdir(filename, FILE_MODE);
		judge_sys_return(ret, "mkdir");
		break;
	default:
		break;
	}

	/* Launch multiple processes to do concurrent operations
	against one file*/
	if (do_multi_process_test == 1) {

		/*Father process add a series of xattr entries first*/
		printf("Test %d: Doing Xattr operations on %s with %d "
		       "processes.\n", testno, filename, child_nums + 1);
		fflush(stdout);
		fflush(stderr);
		for (j = 0; j < xattr_nums; j++) {
			memset(xattr_name, 0, xattr_name_sz + 1);
			memset(xattr_value, 0, xattr_value_sz);
			memset(xattr_value_get, 0, xattr_value_sz);
			if (do_random_test == 1)
				xattr_name_generator(j, ea_nm_class,
						     XATTR_NAME_LEAST_SZ,
						     xattr_name_sz);
			else
				xattr_name_generator(j, ea_nm_class,
						     xattr_name_sz,
						     xattr_name_sz);
			xattr_value_constructor(j);
			ret = add_or_update_ea(ft, fd, XATTR_CREATE, "add");
			if (ret < 0) {
				teardown();
				exit(1);
			}
		}

		signal(SIGCHLD, sigchld_handler);

		/*Propagate a fixed number of children to perform update*/
		for (i = 0; i < child_nums; i++) {
			pid = fork();
			if (pid < 0) {
				fprintf(stderr, "Fork process error!\n");
				teardown();
				exit(1);
			}
			/*Child*/
			if (pid == 0) {
				for (k = 0; k < XATTR_CHILD_UPDATE_TIMES; k++) {
					for (j = 0; j < xattr_nums; j++) {
						strcpy(xattr_name,
						       xattr_name_list_set[j]);
						memset(xattr_value, 0,
						       xattr_value_sz);
						xattr_value_constructor(j);
						ret = add_or_update_ea(ft, fd,
								 XATTR_REPLACE,
								 "update");
						if (ret < 0) {
							teardown();
							exit(1);
						}
						write_buf = realloc(write_buf,
							      CLUSTER_SIZE);
						ftruncate(fd, 0);
						memset(write_buf, 'a'+j%26,
						       CLUSTER_SIZE);
						pwrite(fd, write_buf,
						       CLUSTER_SIZE, 0);
					}
				}
				free(write_buf);
				exit(0);
			}
			if (pid > 0)
				child_pid_list[i] = pid;
		}
		/*Father*/
		signal(SIGINT, sigint_handler);
		signal(SIGTERM, sigterm_handler);
		atexit(atexit_hook);
		for (k = 0; k < XATTR_CHILD_UPDATE_TIMES * 2; k++) {
			for (j = 0; j < xattr_nums; j++) {
				memset(xattr_value_get, 0, xattr_value_sz);
				strcpy(xattr_name, xattr_name_list_set[j]);
				ret = read_ea(ft, fd);
				if (ret < 0) {
					teardown();
					exit(1);
				}
				if (strcmp(xattr_value_get, "") == 0) {
					if (xattr_value_sz < 80) {
						fprintf(stderr, "Read empty "
							"data error when value"
							" size < 80\n");
						teardown();
						exit(1);
					}
				}
				ret = xattr_value_validator(j);
				if (ret < 0) {
					teardown();
					exit(1);
				}
			}
		}
		sleep(10);
		testno++;
		return;
	}
	/* Do normal update/add test*/
	printf("Test %d: Doing normal %lu EAs adding and updating on file %s.\n",
		testno, xattr_nums, filename);

	fflush(stdout);

	for (j = 0; j < xattr_nums; j++) {
		memset(xattr_name, 0, xattr_name_sz + 1);
		memset(xattr_value, 0, xattr_value_sz);
		memset(xattr_value_get, 0, xattr_value_sz);
		if (do_random_test == 1)
			xattr_name_generator(j, ea_nm_class,
					     XATTR_NAME_LEAST_SZ,
					     xattr_name_sz);
		else
			xattr_name_generator(j, ea_nm_class, xattr_name_sz,
					     xattr_name_sz);
		memset(xattr_value, 'w', xattr_value_sz - 1);
		xattr_value[xattr_value_sz - 1] = '\0';
		/*add EA entry*/
		ret = add_or_update_ea(ft, fd, XATTR_CREATE, "add");
		if (ret < 0) {
			teardown();
			exit(1);
		}
		ret = read_ea(ft, fd);
		if (ret < 0) {
			teardown();
			exit(1);
		}
		if (strcmp(xattr_value, xattr_value_get) != 0) {
			fprintf(stderr, "Inconsistent Xattr Value Readed!\n");
			teardown();
			exit(1);
		}
		/*update EA entry here */
		memset(xattr_value, 0, xattr_value_sz);
		memset(xattr_value_get, 0, xattr_value_sz);
		memset(xattr_value, 'z', xattr_value_sz - 1);
		xattr_value[xattr_value_sz - 1] = '\0';
		ret = add_or_update_ea(ft, fd, XATTR_REPLACE, "update");
		if (ret < 0) {
			teardown();
			exit(1);
		}
		ret = read_ea(ft, fd);
		if (ret < 0) {
			teardown();
			exit(1);
		}
		if (strcmp(xattr_value, xattr_value_get) != 0) {
			fprintf(stderr, "Inconsistent Xattr Value Readed!\n");
			teardown();
			exit(1);
		}
	}
	testno++;

	/*Here we do random_size update and check*/
	if (!do_random_test)
		goto list_test;
	printf("Test %d: Doing randomsize updating for %lu EAs on file %s.\n",
		testno, xattr_nums, filename);

	unsigned long update_iter;
	for (update_iter = 0; update_iter < XATTR_RANDOMSIZE_UPDATE_TIMES;
	     update_iter++)
	for (j = 0; j < xattr_nums; j++) {
		memset(xattr_value, 0, xattr_value_sz);
		memset(xattr_value_get, 0, xattr_value_sz);
		memset(xattr_name, 0, xattr_name_sz + 1);
		strcpy(xattr_name, xattr_name_list_set[j]);
		xattr_value_generator(j, XATTR_VALUE_LEAST_SZ, xattr_value_sz);
		if (j % 2 == 0) {
			/*Random size update*/
			ret = add_or_update_ea(ft, fd, XATTR_REPLACE, "update");
			if (ret < 0) {
				teardown();
				exit(1);
			}
		} else {
			/*Remove then add*/
			ret = remove_ea(ft, fd);
			if (ret < 0) {
				teardown();
				exit(1);
			}
			memset(xattr_name, 0, xattr_name_sz + 1);
			xattr_name_generator(j, ea_nm_class,
					     XATTR_NAME_LEAST_SZ,
					     xattr_name_sz);
			ret = add_or_update_ea(ft, fd, XATTR_CREATE, "add");
			if (ret < 0) {
				teardown();
				exit(1);
			}
		}

		ret = read_ea(ft, fd);
		if (ret < 0) {
			teardown();
			exit(1);
		}
		if (strcmp(xattr_value, xattr_value_get) != 0) {
			fprintf(stderr, "Inconsistent Xattr Value Readed!\n");
			teardown();
			exit(1);
		}
	}
	testno++;

list_test:
	/*If the name list did not exceed the limitation of list_sz,
	we do list following*/
	if (!do_list)
		goto bail;
	/*List all EA names if xattr_nums *(xattr_name_sz+1) less than 65536*/
	for (j = 0; j < xattr_nums; j++)
		memset(xattr_name_list_get[j], 0, xattr_name_sz + 1);

	printf("Test %d: Listing all replaced EAs on file %s.\n", testno,
	       filename);
	switch (ft) {
	case NORMAL:
		ret = flistxattr(fd, (void *)list, list_sz);
		judge_sys_return(ret, "flistxattr");
		break;
	case SYMLINK:
		ret = llistxattr(filename, (void *)list, list_sz);
		judge_sys_return(ret, "flistxattr");
		break;
	case DIRECTORY:
		ret = listxattr(filename, (void *)list, list_sz);
		judge_sys_return(ret, "flistxattr");
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
			teardown();
			exit(1);
		}
	}
	testno++;
bail:
	if (keep_ea == 0) {
		printf("Test %d: Removing all EAs on file %s.\n", testno,
		        filename);
		for (j = 0; j < xattr_nums; j++) {
			memset(xattr_name, 0, xattr_name_sz + 1);
			strcpy(xattr_name, xattr_name_list_set[j]);
			ret = remove_ea(ft, fd);
			if (ret < 0) {
				teardown();
				exit(1);
			}

		}
		testno++;

		printf("Test %d: Verifying if all EAs removed from file %s.\n",
		       testno, filename);
		char *veri_list;
		unsigned long veri_list_sz;
		veri_list_sz = (xattr_name_sz + 1) * xattr_nums;
		veri_list = (char *)malloc(veri_list_sz);
		switch (ft) {
		case NORMAL:
			ret = flistxattr(fd, (void *)veri_list,
					 veri_list_sz);
			judge_sys_return(ret, "flistxattr");
			break;
		case SYMLINK:
			ret = llistxattr(filename, (void *)veri_list,
					 veri_list_sz);
			judge_sys_return(ret, "flistxattr");
			break;
		case DIRECTORY:
			ret = listxattr(filename, (void *)veri_list,
					veri_list_sz);
			judge_sys_return(ret, "flistxattr");
			break;
		default:
			break;

		}
		if (strcmp(veri_list, "") != 0) {
			fprintf(stderr, "Remove all EAs failed!\n");
			free((void *)veri_list);
			teardown();
			exit(1);
		}
		free((void *)veri_list);
		testno++;
	}
	/*Unlink the file*/
#ifdef DO_UNLINK
	printf("Test %d: Removing file %s...\n", testno, filename);
	switch (ft) {
	case NORMAL:
		ret = unlink(filename);
		judge_sys_return(ret, "unlink");
		close(fd);
		break;
	case SYMLINK:
		ret = unlink(filename);
		judge_sys_return(ret, "unlink");
		break;
	case DIRECTORY:
		ret = rmdir(filename);
		judge_sys_return(ret, "rmdir");
		break;
	default:
		break;
	}
	testno++;

#endif
	return;
}

static void test_runner(void)
{
	int i;

	for (i = 0; i < iter_nums; i++) {

		printf("<<<Round %d Test Running >>>\n", i);
		one_round_run(ea_filetype, i);
		printf("<<<Round %d Test Succeed>>>\n", i);

	}
}

int main(int argc, char *argv[])
{

	setup(argc, argv);
	test_runner();
	teardown();
	exit(0);
}
