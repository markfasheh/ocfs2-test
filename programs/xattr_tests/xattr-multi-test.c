/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr-multi-test.c
 *
 * A mpi compatible program  for xattr multiple-nodes testing,
 * it was based on testcases designed for xattr-test.c,which
 * will be executed concurrently among multiple nodes.
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
#include <mpi.h>


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

static int rank = -1, size;
static char hostname[HOSTNAME_MAX_SZ];

static enum EA_NAMESPACE_CLASS ea_nm_class = USER;
static enum FILE_TYPE ea_filetype = NORMAL;

static int do_list = 1;
static int do_random_test;
static int only_do_add_test;
static int keep_ea;

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
	       "[-o] [-k] [-r] <path> \n\n"
	       "<iterations> defaults to %d.\n"
	       "<EA_nums> defaults to %d.\n"
	       "<EA_namespace> defaults to user,currently,can be user,system,"
	       "trusted and security.\n"
	       "<EA_name_length> defaults to %d,more than %d,less than .%d\n"
	       "<EA_value_size> defaults to %d,more than %d,less than %d.\n"
	       "<File_type> defaults to common file,can be normal,"
	       "directory and symlink.\n"
	       "[-k] keep the EA entries after test.\n"
	       "[-r] Do test in a random way.\n"
	       "[-o] Only do concurrent add test.\n"
	       "<path> is required.\n"
	       "Will rotate up to <iterations> times.\n"
	       "In each pass, will create a series of files,"
	       "symlinks and directories,in the directory,"
	       "which <path> specifies, then do vairous operations against "
	       "Xattr on specified file object.\n", prog, DEFAULT_ITER_NUMS,
	       DEFAULT_XATTR_NUMS, DEFAULT_XATTR_NAME_SZ, XATTR_NAME_LEAST_SZ,
	       XATTR_NAME_MAX_SZ, DEFAULT_XATTR_VALUE_SZ,
	       XATTR_VALUE_LEAST_SZ, XATTR_VALUE_MAX_SZ);

	MPI_Finalize();

	exit(1);
}

static void abort_printf(const char *fmt, ...)
{
	va_list       ap;

	printf("%s (rank %d): ", hostname, rank);

	va_start(ap, fmt);

	vprintf(fmt, ap);

	MPI_Abort(MPI_COMM_WORLD, 1);
}

static int parse_opts(int argc, char **argv);

static void setup(int argc, char *argv[])
{
	unsigned long i;
	int ret;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Init failed: %d\n", ret);
		exit(1);
	}

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	if (only_do_add_test == 1)
		xattr_name_sz = 50;

	xattr_name = (char *)malloc(xattr_name_sz + 1);
	name_get = (char *)malloc(xattr_name_sz + 1);
	xattr_value = (char *)malloc(xattr_value_sz);
	xattr_value_get = (char *)malloc(xattr_value_sz);
	xattr_name_list_set = (char **)malloc(sizeof(char *) * xattr_nums);

	for (i = 0; i < xattr_nums; i++)
		xattr_name_list_set[i] = (char *)malloc(xattr_name_sz + 1);

	list_sz = (unsigned long)((xattr_name_sz + 1) * xattr_nums);
	if (list_sz > XATTR_LIST_MAX_SZ) {
		do_list = 0;
		fprintf(stderr, "Warning:list size exceed,due to "
			"(xattr_name_sz + 1) * xattr_nums was "
			"greater than 65536,");
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

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0) {
		perror("gethostname:");
		exit(1);
	}

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);
	ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);

	return;
}

static void teardown(int ret_type)
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

	if (ret_type == MPI_RET_SUCCESS) {
		MPI_Finalize();
		exit(0);
	} else {
		fprintf(stderr, "Rank:%d on Host(%s) abort!\n",
			rank, hostname);
		MPI_Abort(MPI_COMM_WORLD, 1);
		exit(1);
	}
}

static int parse_opts(int argc, char **argv)
{
	int c;
	while (1) {
		c = getopt(argc, argv, "i:x:I:X:n:N:l:L:s:S:n:N:kRt:KrOoT:");
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
			strcpy(xattr_namespace_prefix, optarg);
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
		case 'o':
		case 'O':
			only_do_add_test = 1;
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
	char err_str[50];

	snprintf(err_str, 50, "%s,by Rank %d on Hostname(%s)",
		sys_func, rank, hostname);

	if (ret < 0) {
		perror(err_str);
		teardown(MPI_RET_FAILED);
	}

	return;
}

static int one_round_run(enum FILE_TYPE ft, int round_no)
{
	unsigned long i, j;
	int fd, ret;
	DIR *dp;

	char write_buf[100];

	MPI_Request request;
	MPI_Status  status;

	testno = 1;

	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("First MPI_Barrier failed: %d\n", ret);

	memset(write_buf, 0, 100);
	strcpy(write_buf, "Message to be appended!");
	memset(filename, 0, MAX_FILENAME_SZ + 1);

	if (do_list)
		memset(list, 0, list_sz);

	snprintf(filename, MAX_FILENAME_SZ, "%s/test_%s-%d",
		 path, file_type, round_no);

	/* rank 0 responsible for file creation */
	if (rank == 0) {
		switch (ft) {
		case NORMAL:
			fd = open(filename, FILE_FLAGS_CREATE, FILE_MODE);
			judge_sys_return(fd, "open");
			printf("Test %d: Creating commonfile %s on %s(rank 0),"
			       "to perform %lu EAs.\n",
			       testno, filename, hostname, xattr_nums);
			break;
		case SYMLINK:
			ret = symlink("/no/such/file", filename);
			judge_sys_return(ret, "symlink");
			printf("Test %d: Creating symlink %s on %s(rank 0),"
			       "to perform %lu EAs.\n",
			       testno, filename, hostname, xattr_nums);
			break;
		case DIRECTORY:
			ret = mkdir(filename, FILE_MODE);
			judge_sys_return(ret, "mkdir");
			printf("Test %d: Creating directory %s on %s(rank 0),"
			       "to perform %lu EAs.\n",
			       testno, filename, hostname, xattr_nums);
			break;
		default:
			break;

		}

		fflush(stdout);
		testno++;
	}

	/*all process need to wait file to be created by rank 0*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Second MPI_Barrier failed: %d\n", ret);

	/*Rest ranks need to open the file*/
	if (rank != 0) {

		switch (ft) {
		case NORMAL:
			fd = open(filename, FILE_FLAGS_CREATE, FILE_MODE);
			judge_sys_return(fd, "open");
			break;
		case SYMLINK:
			break;
		case DIRECTORY:
			break;
		default:
			break;

		}
	}

	/*wait all process to achieve the file handler*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
	abort_printf("Third MPI_Barrier failed: %d\n", ret);

	/*only do the concurrent add testing during multiple nodes*/
	if (only_do_add_test == 1) {
		if (rank == 0) {
			printf("Test %d: Performancing Xattr operations on %s,"
			       "all ranks take race to add %lu EAs.\n",
			       testno, filename, xattr_nums);
		}
		for (j = 0; j < xattr_nums; j++) {
			memset(xattr_name, 0, xattr_name_sz + 1);
			memset(xattr_value, 0, xattr_value_sz);
			snprintf(xattr_name, xattr_name_sz, "%s.%s-rank%d-%lu",
				 xattr_namespace_prefix, hostname, rank, j);
			if (do_random_test == 1)
				xattr_value_generator(j, XATTR_VALUE_LEAST_SZ,
						      xattr_value_sz);
			else {
				memset(xattr_value, 'e', xattr_value_sz - 1);
				xattr_value[xattr_value_sz - 1] = '\0';
			}

			ret = add_or_update_ea(ft, fd, XATTR_CREATE, "add");
			if (ret < 0)
				teardown(MPI_RET_FAILED);
		}
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Barrier failed: %d\n", ret);
		testno++;
		goto unlink;

	}

	/*Do regular concurrent operations(update/add/remove) testing*/
	if (rank == 0) {
		printf("Test %d: Performancing Xattrs on %s,all ranks take "
		       "race to do update/remove/add/read %lu EAs.\n",
		       testno, filename, xattr_nums);

		fflush(stdout);
	}

	for (j = 0; j < xattr_nums; j++) {
		memset(xattr_name, 0, xattr_name_sz + 1);
		memset(xattr_value, 0, xattr_value_sz);
		memset(xattr_value_get, 0, xattr_value_sz);

		/* Rank 0 help to generate the EA name in random */
		if (rank == 0) {
			if (do_random_test == 1)
				xattr_name_generator(j, ea_nm_class,
						     XATTR_NAME_LEAST_SZ,
						     xattr_name_sz);
			else
				xattr_name_generator(j, ea_nm_class,
						     xattr_name_sz,
						     xattr_name_sz);
			for (i = 1; i < size; i++) {
				ret = MPI_Isend(xattr_name, xattr_name_sz + 1,
						MPI_BYTE, i, 1, MPI_COMM_WORLD,
						&request);
				if (ret != MPI_SUCCESS)
					abort_printf("MPI_Isend failed: %d\n",
						     ret);
				MPI_Wait(&request, &status);

			}
		}
		/*None-root ranks responsible for EA operations*/
		else {
			MPI_Irecv(xattr_name, xattr_name_sz + 1, MPI_BYTE,
				  0, 1, MPI_COMM_WORLD, &request);
			MPI_Wait(&request, &status);
			strcpy(xattr_name_list_set[j], xattr_name);
		}
		/*Wait all ranks get the xattr name sent by rank 0*/
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Barrier failed: %d\n", ret);
		/*Rank 0 First add the EA entry for rest noeds updating*/
		if (rank == 0) {
			xattr_value_constructor(j);
			ret = add_or_update_ea(ft, fd, XATTR_CREATE, "add");
			if (ret < 0)
				teardown(MPI_RET_FAILED);
			ret = read_ea(ft, fd);
			if (ret < 0)
				teardown(MPI_RET_FAILED);

			ret = xattr_value_validator(j);
			if (ret < 0)
				teardown(MPI_RET_FAILED);

		}

		/* None-root Ranks need to wait the completion of EA
		adding by rank 0 */
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("Forth MPI_Barrier failed %d\n", ret);

		/*Rest ranks take a race to perform update action on newly
		added EA,where rank0 take a race to read*/
		if (rank != 0) {
			/*Here we do random_size update and check*/
			memset(xattr_value, 0, xattr_value_sz);
			memset(xattr_value_get, 0, xattr_value_sz);
			xattr_value_constructor(j);
			ret = add_or_update_ea(ft, fd, XATTR_REPLACE, "update");
			if (ret < 0 )
				teardown(MPI_RET_FAILED);
			if (ft == NORMAL)
				ret = write(fd, write_buf, 100);
		}
		/*Rank 0 take a race to read*/
		else {
			memset(xattr_value_get, 0, xattr_value_sz);
			ret = read_ea(ft, fd);
			if (ret < 0)
				teardown(MPI_RET_FAILED);
			if (strcmp(xattr_value_get, "") == 0) {
				if (xattr_value_sz < 80) {
					fprintf(stderr, "Read emtpy data error "
						"when value size < 80\n");
					teardown(MPI_RET_FAILED);
					exit(1);
				}
			}
			ret = xattr_value_validator(j);
			if (ret < 0)
				teardown(MPI_RET_FAILED);

		}
		/* All Ranks need to wait the completion of
		one EA's operation */
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("Forth MPI_Barrier failed %d\n", ret);
	}
	testno++;

/*ranks take a race to list the EAs*/
list_test:
	/*If the name list did not exceed the limitation of list_sz,
	we do list following*/
	if (!do_list)
		goto bail;
	/*List all EA names if xattr_nums *(xattr_name_sz + 1) less than 65536*/
	for (j = 0; j < xattr_nums; j++)
		memset(xattr_name_list_get[j], 0, xattr_name_sz + 1);

	if (rank == 0)
		printf("Test %d: Listing all EAs of file %s by all nodes.\n",
	       	       testno, filename);

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
			fprintf(stderr, "Xattr list name(%s)  did not match"
				" the orginal one\n", xattr_name_list_get[j]);
			teardown(MPI_RET_FAILED);
		}
	}
	testno++;
	/*Need to wait the completion of all list operation by ranks*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Fifth MPI_Barrier failed %d\n", ret);



/*ranks take race to remove all EAs*/
bail:
	if (keep_ea == 0) {
		if (rank == 0)
			printf("Test %d: Removing all EAs on file %s.\n",
			       testno, filename);
		for (j = 0; j < xattr_nums; j++) {
			if ((j % size) == rank) {
				strcpy(xattr_name, xattr_name_list_set[j]);
				ret = remove_ea(ft, fd);
				if (ret < 0)
					teardown(MPI_RET_FAILED);
			}
		}
		testno++;

		/*Need to wait the completion of all remove operation by ranks*/
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("Sixth MPI_Barrier failed %d\n", ret);

		/*After removal,rank0 verify its emptiness*/
		if (rank == 0) {
			printf("Test %d: Verifying if all EAs removed from file"
			       " %s by rank0.\n", testno, filename);
			char *veri_list;
			unsigned long veri_list_sz;
			veri_list_sz = (xattr_name_sz + 1) * xattr_nums;
			veri_list = (char *)malloc(veri_list_sz);
			memset(veri_list, 0, veri_list_sz);
			switch (ft) {
			case NORMAL:
				ret = flistxattr(fd, (void *)veri_list,
						 veri_list_sz);
				judge_sys_return(ret, "flistxattr");
				break;
			case SYMLINK:
				ret = llistxattr(filename,
						 (void *)veri_list,
						 veri_list_sz);
				judge_sys_return(ret, "flistxattr");
				break;
			case DIRECTORY:
				ret = listxattr(filename,
						(void *)veri_list,
						veri_list_sz);
				judge_sys_return(ret, "flistxattr");
				break;
			default:
				break;
			}

			if (strcmp(veri_list, "") != 0) {
				fprintf(stderr, "Remove all EAs failed!\n");
				free((void *)veri_list);
				teardown(MPI_RET_FAILED);
			}
			free((void *)veri_list);
		}
		testno++;

	}
	/*Need to wait the completion remove operation by ranks*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Seventh MPI_Barrier failed %d\n", ret);


/*Rank 0 Unlink the file*/
unlink:
#ifdef DO_UNLINK
	if (rank == 0) {
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
	}
	testno++;

	/*Need to wait the completion file removal*/
	ret = MPI_Barrier(MPI_COMM_WORLD);
	if (ret != MPI_SUCCESS)
		abort_printf("Eighth MPI_Barrier failed %d\n", ret);
#endif

	return 0;
}
static int test_runner(void)
{
	int i;
	int ret;

	for (i = 0; i < iter_nums; i++) {
		if (rank == 0) {
			printf("**************************************"
			       "****************\n");
			printf("**************Round %d test running..."
			       "*****************\n", i);
			printf("**************************************"
			       "****************\n");
			fflush(stdout);
		}

		one_round_run(ea_filetype, i);

		/* All Ranks need to wait the completion of EA
		operation on one file*/
		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS)
			abort_printf("Nineth MPI_Barrier failed %d\n", ret);
	}
}
int main(int argc, char *argv[])
{

	setup(argc, argv);
	test_runner();
	teardown(MPI_RET_SUCCESS);
}
