/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * quota_multi_tests.c
 *
 * A mpi compatible program to test quota among mutiple nodes by
 * running a series of generic and stress tests.
 *
 * Author:  Tristan Ye (tristan.ye@oracle.com)
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

#include "quota.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <sys/wait.h>
#include <signal.h>
#include <mpi.h>
#include <pwd.h>
#include <grp.h>

#include <ocfs2/ocfs2.h>

#define HOSTNAME_MAX_SZ         100
#define PATH_SZ                 255
#define MAX_FILENAME_SZ         200
#define DEFAULT_ITER_NUMS       1

#define DEFAULT_USER_NUM 	10
#define DEFAULT_GROUP_NUM 	10
#define TEST_NODES_MAX_NUM	32
#define USERNAME_SZ		100
#define GROUPNAME_SZ		100

#define QUOTAON_BIN		"/usr/local/sbin/quotaon"
#define USERADD_BIN		"/usr/sbin/useradd"
#define USERDEL_BIN		"/usr/sbin/userdel"
#define GROUPADD_BIN		"/usr/sbin/groupadd"
#define GROUPDEL_BIN		"/usr/sbin/groupdel"

#define WORKPLACE		"quota-test"

#define USER			1
#define GROUP			2
#define USER_IN_GROUP		4

#define ADD			1
#define REMOVE			2

#define QUOTAUSER		1
#define QUOTAGROUP		2

#define ISOFT_LIMIT		1
#define IHARD_LIMIT		2
#define BSOFT_LIMIT		4
#define BHARD_LIMIT		8

#define FILE_MODE               (S_IRUSR|S_IWUSR|S_IXUSR|S_IROTH|\
                                 S_IWOTH|S_IXOTH|S_IRGRP|S_IWGRP|S_IXGRP)

static char *prog;
static char mountpoint[PATH_SZ + 1];
static char device[PATH_SZ + 1];
static char workplace[PATH_SZ +1];
static char filename[PATH_SZ +1];
static long iterations = DEFAULT_ITER_NUMS;

static long user_nums = DEFAULT_USER_NUM;
static long group_nums = DEFAULT_GROUP_NUM;

static int type = 0;

static int testno = 1;
static int rank, size;
static char hostname[HOSTNAME_MAX_SZ];

static unsigned int blocksize;
static unsigned long clustersize;

ocfs2_filesys *fs;
struct ocfs2_super_block *ocfs2_sb;

static int usage(void)
{

	printf("Usage: quota_multi_tests [-t iterations] [-u users] [-g groups] "
               "<-d device> <mount_point>\n"
               "Run a series of tests intended to verify quota functionality "
               "among multiple nodes.\n\n"
               "-i iterations specify the running times.\n"
               "-u users,specify the number of users.\n"
               "-g groups,specify the number of groups.\n"
               "at least one of the -u or -g options shoud be selected.\n"
               "device and mount_point are mandatory.\n");

	MPI_Finalize();
	exit(1);

}

static void abort_printf(const char *fmt, ...)
{
	va_list	ap;

	printf("%s (rank %d): ", hostname, rank);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	MPI_Abort(MPI_COMM_WORLD, 1);
}

static void log_printf(FILE *stream, const char *fmt, ...)
{
	va_list	ap;

	fprintf(stream, "%s (rank %d): ", hostname, rank);
	va_start(ap,fmt);
	vprintf(fmt, ap);
}

static void root_printf(const char *fmt, ...)
{
        va_list ap;

        if (rank == 0) {
                va_start(ap, fmt);
                vprintf(fmt, ap);
        }
}

static uid_t user2uid(char *name)
{
	struct passwd *entry;
	uid_t ret;

	if (!(entry = getpwnam(name))) {
		ret = errno;
		log_printf(stderr, "user %s does not exist!\n", name);
		return ret;
	} else {
		ret = entry->pw_uid;
		return ret;

	}
}

static uid_t group2gid(char *name)
{
	struct group *entry;
	gid_t ret;

	if (!(entry = getgrnam(name))) {
		ret = errno;
		log_printf(stderr, "group %s does not exist!\n", name);
		return ret;
	} else {
		ret = entry->gr_gid;
		return ret;
	}

}

static int name2id(int type, char *name)
{
	if (type & USER)
		return user2uid(name);
	if (type & GROUP)
		return group2gid(name);
}

static void MPI_Barrier_Sync(void)
{
	int ret;

	ret = MPI_Barrier(MPI_COMM_WORLD);
        if (ret != MPI_SUCCESS)
                abort_printf("MPI_Barrier failed: %d\n", ret);
}

int open_ocfs2_volume(char *device_name)
{
	int open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK | OCFS2_FLAG_RO;
	int ret;

	ret = ocfs2_open(device_name, open_flags, 0, 0, &fs);
	if (ret < 0) {
		fprintf(stderr, "%s is not a ocfs2 volume!\n", device_name);
                return ret;
        }

	ocfs2_sb = OCFS2_RAW_SB(fs->fs_super);
	blocksize = 1 << ocfs2_sb->s_blocksize_bits;
	clustersize = 1 << ocfs2_sb->s_clustersize_bits;

	return 0;
}

static int parse_opts(int argc, char *argv[])
{
	char c;
	while (1) {
		c = getopt(argc, argv, "i:I:u:U:g:G:d:D:t:T:");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
		case 'I':
			iterations = atol(optarg);
			break;
		case 'u':
		case 'U':
			type |= QUOTAUSER;
			user_nums = atol(optarg);
			break;
		case 'g':
		case 'G':
			type |= QUOTAGROUP;
			group_nums = atol(optarg);
			break;
		case 'd':
		case 'D':
			strcpy(device, optarg);
			break;
		case 't':
		case 'T':
			iterations = atol(optarg);
		default:
			return EINVAL;
		}
	}

	if (argc - optind != 1)
                return EINVAL;

	if (!(type & QUOTAUSER) && !(type & QUOTAGROUP)) {
		log_printf(stderr, "At least one of -u or -g options "
			   "should be specified!\n");
		return EINVAL;
	}

	strcpy(mountpoint, argv[optind]);
	if (mountpoint[strlen(mountpoint) - 1] == '/')
		mountpoint[strlen(mountpoint) - 1] = '\0';

	return 0;
}

static int add_rm_user_group(const char *bin_path, int ad_or_rm, int usr_or_grp,
			  const char *name, char *ex_name)
{
	int ret = 0;
	int child_status;
	char *argv[6];
	char arg1[20];
	pid_t child;
	
	child = fork();
	if (child < 0) {
		ret = errno;
		log_printf(stderr, "Failed to fork for user/group add/remove:%d:%s\n",
			   ret, strerror(ret));
		goto bail;
	}
	if (!child) {
		argv[0] = (char *)bin_path;
		
		if (usr_or_grp & USER) {
			if (ad_or_rm & ADD)
				strcpy(arg1, "-m");
			else
				strcpy(arg1, "-r");
			argv[1] = arg1;
			argv[2] = (char *)name;
			argv[3] = NULL;
			
		} else if (usr_or_grp & GROUP) {
			argv[1] = (char *)name;
			argv[2] = NULL;
		} else if (usr_or_grp & USER_IN_GROUP) {
			strcpy(arg1, "-m");
			argv[1] = arg1;
			argv[2] = "-g";
			argv[3] = (char *)ex_name;
			argv[4] = (char *)name;
			argv[5] = NULL;
		}

		ret = execv(argv[0], argv);
		ret = errno;
		exit(ret);
		
	} else {
		ret = waitpid(child, &child_status, 0);
		if (ret < 0) {
			ret = errno;
			log_printf(stderr, "Failed to wait child for its completion"
				   ":%d:%s\n", ret, strerror(ret));
			goto bail;
		}

		ret = WEXITSTATUS(child_status);
	}

bail:
	return ret;
} 

static int quota_on_off(const char *qbin_path, int on, int type,
			const char *fsname)
{
	int ret = 0;
	int child_status;
	char *argv[4];
	char arg1[20];
	pid_t child;

	child = fork();
	if (child < 0) {
		ret = errno;
		log_printf(stderr, "Failed to fork for quota on or off:%d:%s\n",
        		   ret, strerror(ret));
		goto bail;
	}

	if (!child) {
		argv[0] = (char *)qbin_path;
		strcpy(arg1, "-");
		if (type & QUOTAUSER)
			strcat(arg1, "u");
		if (type & QUOTAGROUP)
			strcat(arg1, "g");
		if (!on)
			strcat(arg1, "f");
			
		argv[1] = arg1;
		argv[2] = (char *)fsname;
		argv[3] = NULL;

		ret = execv(argv[0], argv);
		ret = errno;
		exit(ret);

	} else {
		ret = waitpid(child, &child_status, 0);
		if (ret < 0) {
			ret = errno;
			log_printf(stderr, "Failed to wait child for its completion"
				   ":%d:%s\n", ret, strerror(ret));
			goto bail;
		}
		
		ret = WEXITSTATUS(child_status);
	}

bail:
	return ret;
}

static int setquotainfo(int type, const char *device, int id, struct if_dqinfo dqi)
{
	int ret;
	int q_type;
	
	if (type & QUOTAUSER)
		q_type = USRQUOTA;
	else
		q_type = GRPQUOTA;

	if (quotactl(QCMD(Q_SETINFO, q_type), device, id, (caddr_t) & dqi)) {
		ret = errno;
		log_printf(stderr, "Set quota info failed,quotactl:%d:%s\n",
			   ret, strerror(ret));
	} else
		ret = 0;

	return ret;
}

static int setquota(int type, const char *device, int id, struct if_dqblk dq)
{
	int ret;
	int q_type;

	if (type & QUOTAUSER)
		q_type = USRQUOTA;
	else
		q_type = GRPQUOTA;

	if (quotactl(QCMD(Q_SETQUOTA, q_type), device, id, (caddr_t) & dq)) {
		ret = errno;
		log_printf(stderr, "Set quota failed,quotactl:%d:%s\n",
			   ret, strerror(ret));
	} else
		ret = 0;
	
	return ret;
}

static int getquotainfo(int type, const char *device, int id, struct if_dqinfo *dqi)
{
	int ret;
	int q_type;

	if (type & QUOTAUSER)
		q_type = USRQUOTA;
	else
		q_type = GRPQUOTA;

	if (quotactl(QCMD(Q_GETINFO, q_type), device, id, (caddr_t)dqi)) {
		ret = errno;
		log_printf(stderr, "Get quota info failed, quotactl:%d:%s\n",
			   ret, strerror(ret));
	} else
		ret = 0;

	return ret;
}

static int getquota(int type, const char *deive, int id, struct if_dqblk *dq)
{
	int ret;
	int q_type;

	if (type & QUOTAUSER)
		q_type = USRQUOTA;
	else
		q_type = GRPQUOTA;

	if (quotactl(QCMD(Q_GETQUOTA, q_type), device, id, (caddr_t)dq)) {
		ret = errno;
		log_printf(stderr, "Get quota failed,quotactl:%d:%s\n",
			   ret, strerror(ret));
	} else
		ret = 0;
	
	return ret;
}

static int verify_quota_items(const struct if_dqblk s_dq, const struct if_dqblk d_dq,
			int limit_type)
{
	int ret = 1;

	if (limit_type & ISOFT_LIMIT) {

		if (s_dq.dqb_isoftlimit != d_dq.dqb_isoftlimit) {
			log_printf(stderr, "Quota inconsistent match found,"
				   "expected isoftlimit = %d,actual isoftlimit = %d\n",
				   s_dq.dqb_isoftlimit, d_dq.dqb_isoftlimit);
			ret = 0;
			goto bail;
		}
	}

	if (limit_type & IHARD_LIMIT) {
		if (s_dq.dqb_ihardlimit != d_dq.dqb_ihardlimit) {
			log_printf(stderr, "Quota inconsistent match found,"
				   "expected ihardlimit = %d,actual ihardlimit = %d\n",
				   s_dq.dqb_ihardlimit, d_dq.dqb_ihardlimit);
			ret = 0;
			goto bail;
		}
	}

	if (limit_type & BSOFT_LIMIT) {

		if (s_dq.dqb_bsoftlimit != d_dq.dqb_bsoftlimit) {
			log_printf(stderr, "Quota inconsistent match found,"
				   "expected bsoftlimit = %d,actual bsoftlimit = %d\n",
				   s_dq.dqb_bsoftlimit, d_dq.dqb_bsoftlimit);
			ret = 0;
			goto bail;
		}
	}

	if (limit_type & BHARD_LIMIT) {
		if (s_dq.dqb_bhardlimit != d_dq.dqb_bhardlimit) {
			log_printf(stderr, "Quota inconsistent match found,"
				   "expected bhardlimit = %d,actual bhardlimit = %d\n",
				   s_dq.dqb_bhardlimit, d_dq.dqb_bhardlimit);
			ret = 0;
			goto bail;
		}
	}

bail:
	return ret;

}

static unsigned long get_rand(unsigned long min, unsigned long max)
{
        if (min == 0 && max == 0)
                return 0;

        return min + (rand() % (max - min + 1));
}

static void negative_inodes_limit_test(long isoftlimit, long bsoftlimit,
				       long user_postfix, long rm_nums)
{
	int ret, fd, o_uid, j;
	long i, file_index, rm_counts = 0;
	struct if_dqblk s_dq, d_dq;
	char username[USERNAME_SZ];
	int *inodes_removed;
	
	MPI_Request request;
        MPI_Status status;

	if (!rank) {

		inodes_removed = (int *)malloc(sizeof(int) * isoftlimit * 2);
		memset((void *)inodes_removed, 0, sizeof(int) * isoftlimit *2);
		snprintf(username, USERNAME_SZ, "quota-user-rank%d-%d", rank,
			 user_postfix);

		add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);
		getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
		s_dq.dqb_isoftlimit = isoftlimit;
		s_dq.dqb_ihardlimit = isoftlimit * 2;
		s_dq.dqb_bsoftlimit = bsoftlimit;
		s_dq.dqb_bhardlimit = bsoftlimit * 2;
		s_dq.dqb_curinodes = 0;
		s_dq.dqb_curspace = 0;
		setquota(QUOTAUSER, device, name2id(USER, username), s_dq);
	} else
		snprintf(username, USERNAME_SZ, "quota-user-rank0-%d",
			 user_postfix);

	/*
	 * Rank0 creats the files,while other ranks take race to remove.
	 * Thus,the quota number should go negative.
	*/

	if (!rank) {
		o_uid = getuid();
		seteuid(name2id(USER, username));
		for (i = 0; i < isoftlimit * 2; i++) {
			snprintf(filename, PATH_SZ, "%s/%s-quotafile-%d",
				 workplace, username, i);
			fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
			if (fd < 0) {
			        ret = errno;
			        abort_printf("Open file failed:%d:%s\n", ret, strerror(ret));
			}
			
			close(fd);
		}

		seteuid(o_uid);
		
		rm_counts = 0;
		for (j = 1; j < size; j++) {
			for (i = 0; i < rm_nums; i++) {
				ret = MPI_Irecv(&file_index, sizeof(long),
						MPI_BYTE, j, 1, MPI_COMM_WORLD,
						&request);
				
				if (ret == MPI_SUCCESS) {
					rm_counts++;
					inodes_removed[file_index] = 1;
				}
				else
					abort_printf("MPI_Irecv Failed.\n");
				MPI_Wait(&request, &status);
			}
		}

		getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);
		if (d_dq.dqb_curinodes != isoftlimit * 2 - rm_counts)
			abort_printf("Negative inodes test failed among nodes,"
				     "Incorrect quota stats found,expected "
				     "inodes_num = %ld, queried inodes_num = "
				     "%ld.\n", isoftlimit * 2 - rm_counts, 
				     d_dq.dqb_curinodes); 
		
	} else {
		/*
		 * Other nodes perform a random deletion as root user
		*/
		rm_counts = 0;
		
		while (rm_counts < rm_nums) {
			i = get_rand(0, isoftlimit * 2 - 1);
			snprintf(filename, PATH_SZ, "%s/%s-quotafile-%d",
				 workplace, username, i);
			ret = unlink(filename);
			if (ret < 0)
				continue;
			else {
				ret = MPI_Isend(&i, sizeof(long), MPI_BYTE,
						0, 1, MPI_COMM_WORLD, &request);
				if (ret != MPI_SUCCESS)
					abort_printf("MPI_Isend Failed.\n");
				MPI_Wait(&request, &status);
				rm_counts++;
			}
		}
	}

	MPI_Barrier_Sync();
	/* Cleanup */

	if (!rank) {
		file_index = 0;
		while (file_index < isoftlimit * 2) {
			if (!inodes_removed[file_index]) {
				snprintf(filename, PATH_SZ, "%s/%s-quotafile-%d",
					 workplace, username, file_index);
				ret = unlink(filename);
			}

			file_index++;
		}

	snprintf(filename, PATH_SZ, "%s/%s-quotafile-%d", workplace, username, 0);
	ret = access(filename, F_OK);
	if (ret == 0)
		unlink(filename);

	add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);

	}
}

static void user_inodes_grace_time_test(long isoftlimit, long bsoftlimit,
					long grace_seconds, long user_postfix)
{
	int ret, fd, o_uid;
	long i;
	struct if_dqblk s_dq, d_dq;
	struct if_dqinfo s_dqi;

	char username[USERNAME_SZ];

	snprintf(username, USERNAME_SZ, "quota-user-rank%d-%d", rank,
		 user_postfix);
	
	add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);
	getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
	s_dq.dqb_ihardlimit = isoftlimit * 2;
	s_dq.dqb_bsoftlimit = bsoftlimit;
	s_dq.dqb_bhardlimit = bsoftlimit * 2;
	s_dq.dqb_curinodes = 0;
	s_dq.dqb_curspace = 0;
	setquota(QUOTAUSER, device, name2id(USER, username), s_dq);

	getquotainfo(QUOTAUSER, device, name2id(USER, username), &s_dqi);
	s_dqi.dqi_bgrace = 60000;
	s_dqi.dqi_igrace = grace_seconds;
	setquotainfo(QUOTAUSER, device, name2id(USER, username), s_dqi);

	o_uid = getuid();
	seteuid(name2id(USER, username));

	for (i = 0; i <= isoftlimit ; i++) {
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			abort_printf("Open file failed:%d:%s\n", ret, strerror(ret));
		}

		close(fd);
                getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);
                if (d_dq.dqb_curinodes != i + 1)
                        abort_printf("Incorrect quota stats found,expected "
                                     "inode_num = %d,queried inode_num = %d.\n",
                                     i + 1, d_dq.dqb_curinodes);
        }

	/*Grace time take effect from now*/
	sleep(grace_seconds);
	/*grace time expires,so should hit failure here*/
        snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
                 workplace, hostname, username, isoftlimit + 1);
        if ((fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE)) > 0) {
                close(fd);
                abort_printf("Not allowd to exceed the grace time limit of inodes.\n");
        }

	/*cleanup*/
	seteuid(o_uid);

	for (i = 0; i <= isoftlimit; i++) {
                snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		ret = unlink(filename);
		if (ret < 0) {
			ret =errno;
			abort_printf("Failed to unlink file(%s):%d:%s\n",
				     filename, ret, strerror(ret));
		}

        }

        add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
}

static void user_space_limit_test(long isoftlimit, long bsoftlimit, int user_postfix)
{
	int ret;
	long i;
	int fd;
	int o_uid;
	struct if_dqblk s_dq, d_dq;
	char *write_buf;
	int writen_sz = 0;
	long file_sz = 0;
	char username[USERNAME_SZ];

	write_buf = (char *)malloc(clustersize);
	memset(write_buf, 0, clustersize);
	
	snprintf(username, USERNAME_SZ, "quotauser-rank%d-%d", rank,
		 user_postfix);
	add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);
	getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
        s_dq.dqb_ihardlimit = isoftlimit * 2;
        s_dq.dqb_bsoftlimit = bsoftlimit;
        s_dq.dqb_bhardlimit = bsoftlimit * 2;
        s_dq.dqb_curinodes = 0;
        s_dq.dqb_curspace = 0;
	setquota(QUOTAUSER, device, name2id(USER, username), s_dq);

	o_uid = getuid();

	ret = seteuid(name2id(USER, username));
	if (ret < 0) {
		ret = errno;
		abort_printf("Set euid failed:%d:%s.\n", ret, strerror(ret));
	}

	snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-user-spacelimit", workplace,
		 hostname, username);

	fd = open(filename, O_RDWR | O_CREAT | O_TRUNC | O_APPEND, FILE_MODE);

	while (file_sz + clustersize <= bsoftlimit * 2 * 1024) {
		writen_sz = write(fd, write_buf, clustersize);
		if (writen_sz < 0) {
			ret = errno;
			abort_printf("write failed:%d:%s.\n", ret, strerror(ret));
		}
		file_sz += writen_sz;
	}

	if ((ret = write(fd, write_buf, clustersize)) > 0)
		abort_printf("No allowed to exceed the hard limit of space");

	if (fd)
		close(fd);

	ret = unlink(filename);
	if (ret < 0) {
		ret =errno;
		abort_printf("Failed to unlink file(%s):%d:%s\n",
			     filename, ret, strerror(ret));
        }

	sync();

	seteuid(o_uid);

	add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);

	if (write_buf)
		free(write_buf);
}

static void user_inodes_limit_test(long isoftlimit, long bsoftlimit, int user_postfix)
{
	int ret;
	long i;
	int fd;
	int o_uid;
	struct if_dqblk s_dq, d_dq;

        char username[USERNAME_SZ];

	snprintf(username, USERNAME_SZ, "quotauser-rank%d-%d", rank,
		 user_postfix);
	add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);
	getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
        s_dq.dqb_ihardlimit = isoftlimit * 2;
        s_dq.dqb_bsoftlimit = bsoftlimit; 
        s_dq.dqb_bhardlimit = bsoftlimit * 2;
	s_dq.dqb_curinodes = 0;
	s_dq.dqb_curspace = 0;
	setquota(QUOTAUSER, device, name2id(USER, username), s_dq);
	
	o_uid = getuid();
	ret =  seteuid(name2id(USER, username));
	if (ret < 0) {
		ret = errno;
		abort_printf("Set euid failed:%d:%s.\n", ret, strerror(ret));
	}

	for (i = 0; i < isoftlimit * 2; i++) {
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		if (!(i % 2)) {
			fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
			if (fd < 0) {
				ret = errno;
				abort_printf("Open file failed:%d:%s\n", ret, strerror(ret));
			}
			close(fd);
		} else
			mkdir(filename, FILE_MODE);

		getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);
		if (d_dq.dqb_curinodes != i + 1)
			abort_printf("Incorrect quota stats found,expected "
				     "inode_num = %d,queried inode_num = %d.\n",
				     i + 1, d_dq.dqb_curinodes);
	}

	/*We definitely should hit falure here*/
	snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
		 workplace, hostname, username, isoftlimit * 2);

	if ((fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE)) > 0) {
		close(fd);
		abort_printf("Not allowd to exceed the hard limit of inodes.\n");
	}

	/*cleanup*/
	seteuid(o_uid);
	for (i = 0; i < isoftlimit * 2; i++) {
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		if (!(i % 2)) {
			ret = unlink(filename);
			if (ret < 0) {
				ret =errno;
				abort_printf("Failed to unlink file(%s):%d:%s\n",
					     filename, ret, strerror(ret));
			}
		} else {
			ret = rmdir(filename);
			if (ret < 0) {
				ret = errno;
				abort_printf("Failed to remove dir(%s):%d:%s.\n",
					     filename, ret, strerror(ret));
			}
		}

	}

	add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
}

static void group_space_limit_test(long isoftlimit, long bsoftlimit,
				   long user_num, int grp_postfix)
{
	int ret, fd;
	long i, j;
	int o_uid, o_gid;
	struct if_dqblk s_dq, d_dq;

	char username[USERNAME_SZ], groupname[GROUPNAME_SZ];
	char *write_buf;
	int writen_sz, user_index;
	long file_sz = 0, count = 0;

	write_buf = (char *)malloc(clustersize);
	memset(write_buf, 0, clustersize);

	snprintf(groupname, GROUPNAME_SZ, "quotagroup-rank%d-%d", rank,
		 grp_postfix);
	add_rm_user_group(GROUPADD_BIN, ADD, GROUP, groupname, NULL);
	getquota(QUOTAGROUP, device, name2id(GROUP, groupname), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
	s_dq.dqb_ihardlimit = isoftlimit * 2;
	s_dq.dqb_bsoftlimit = bsoftlimit; 
	s_dq.dqb_bhardlimit = bsoftlimit * 2;
	s_dq.dqb_curinodes = 0;
	s_dq.dqb_curspace = 0;
	setquota(QUOTAGROUP, device, name2id(GROUP, groupname), s_dq);
	
	o_gid = getgid();
	setegid(name2id(GROUP, groupname));

	for (i = 0; i < user_num; i++) {
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, i);
		add_rm_user_group(USERADD_BIN, ADD, USER_IN_GROUP, username,
				  groupname);
		getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
		s_dq.dqb_isoftlimit = isoftlimit;
		s_dq.dqb_ihardlimit = isoftlimit * 2;
		s_dq.dqb_bsoftlimit = bsoftlimit;
		s_dq.dqb_bhardlimit = bsoftlimit * 2;
		s_dq.dqb_curinodes = 0;
		s_dq.dqb_curspace = 0;
		setquota(QUOTAUSER, device, name2id(USER, username), s_dq);

		o_uid = getuid();
		seteuid(name2id(USER, username));

		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-group-spacelimit",
			 workplace, hostname, username);
		fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
		if (fd < 0)
			abort_printf("Create file failed:%d:%s.\n", ret, strerror(ret));
		close(fd);
		seteuid(o_uid);
	}

        while (file_sz + clustersize <= bsoftlimit * 2 * 1024) {
		user_index = count % user_num;
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, user_index);
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-group-spacelimit",
			 workplace, hostname, username);
		o_uid = getuid();
		seteuid(name2id(USER, username));
		fd = open(filename, O_RDWR | O_APPEND, FILE_MODE);
                writen_sz = write(fd, write_buf, clustersize);
                if (writen_sz < 0) {
                        ret = errno;
                        abort_printf("write failed:%d:%s.\n", ret, strerror(ret));
                }
		close(fd);
		seteuid(o_uid);
                file_sz += writen_sz;
		count++;
        }

	user_index = count % user_num;
	snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, user_index);
	snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-group-spacelimit",
		 workplace, hostname, username);
	o_uid = getuid();
	seteuid(name2id(USER, username));
	fd = open(filename, O_RDWR | O_APPEND, FILE_MODE);
	if ((writen_sz = write(fd, write_buf, clustersize)) > 0)
		abort_printf("Not allowed to exceed space hard limit of group.");

	close(fd);
	seteuid(o_uid);
	setegid(o_gid);

	for (i = 0; i < user_num; i++) {
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, i);
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-group-spacelimit",
			 workplace, hostname, username);
		ret = unlink(filename);
		if (ret < 0) {
			ret = errno;
			abort_printf("Failed to unlink file %s:%d:%s.\n",
				     filename, ret, strerror(ret));
		}
		add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
	}
	
	sync();

	add_rm_user_group(GROUPDEL_BIN, REMOVE, GROUP, groupname, NULL);
	
	if (write_buf)
		free(write_buf);
}

static void group_inodes_limit_test(long isoftlimit, long bsoftlimit,
				    long user_num, int grp_postfix)
{
	int ret, fd;
	long i, j;
	int o_uid, o_gid;
	int user_index;
	struct if_dqblk s_dq, d_dq;

        char username[USERNAME_SZ], groupname[GROUPNAME_SZ];

	snprintf(groupname, GROUPNAME_SZ, "quotagroup-rank%d-%d", rank,
		 grp_postfix);
	add_rm_user_group(GROUPADD_BIN, ADD, GROUP, groupname, NULL);

	getquota(QUOTAGROUP, device, name2id(GROUP, groupname), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
        s_dq.dqb_ihardlimit = isoftlimit * 2;
        s_dq.dqb_bsoftlimit = bsoftlimit; 
        s_dq.dqb_bhardlimit = bsoftlimit * 2;
	s_dq.dqb_curinodes = 0;
	s_dq.dqb_curspace = 0;
	setquota(QUOTAGROUP, device, name2id(GROUP, groupname), s_dq);

	for (i = 0; i < user_num; i++) {
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, i);
		add_rm_user_group(USERADD_BIN, ADD, USER_IN_GROUP, username,
				  groupname);
		getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
		s_dq.dqb_isoftlimit = isoftlimit;
	        s_dq.dqb_ihardlimit = isoftlimit * 2;
		s_dq.dqb_bsoftlimit = bsoftlimit; 
		s_dq.dqb_bhardlimit = bsoftlimit * 2;
		s_dq.dqb_curinodes = 0;
		s_dq.dqb_curspace = 0;
		setquota(QUOTAUSER, device, name2id(USER, username), s_dq);
	}
	
	o_gid = getgid();
	setegid(name2id(GROUP, groupname));

	for (i = 0; i < isoftlimit * 2; i++) {
		user_index = i % user_num;
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname,
			 user_index);
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		o_uid = getuid();
		seteuid(name2id(USER, username));
		fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			abort_printf("Open %d file failed:%d:%s\n", i, ret, strerror(ret));
		}

		close(fd);
		seteuid(o_uid);

		getquota(QUOTAGROUP, device, name2id(GROUP, groupname), &d_dq);
		if (d_dq.dqb_curinodes != i + 1)
			abort_printf("Incorrect quota stats found,expected "
				     "inode_num = %d,queried inode_num = %d.\n",
				     i + 1, d_dq.dqb_curinodes);
	}

	/*We definitely should hit falure here*/
	user_index = (isoftlimit * 2) % user_num;
	snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, user_index);
	snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
		 workplace, hostname, username, isoftlimit * 2);
	o_uid = getuid();
	seteuid(name2id(USER, username));
	if ((fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE)) > 0) {
		close(fd);
		abort_printf("Not allowd to exceed the hard limit of inodes.\n");
	}

	seteuid(o_uid);
	setegid(o_gid);

	/*cleanup*/
	for (i = 0; i < isoftlimit * 2; i++) {
		user_index = i % user_num;
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname,
			 user_index);
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
			ret = unlink(filename);
		if (ret < 0) {
			ret =errno;
			abort_printf("Failed to unlink file(%s):%d:%s\n",
				     filename, ret, strerror(ret));
		}

	}
	
	for (i = 0; i < user_num; i++) {
		snprintf(username, USERNAME_SZ, "%s-quotauser-%d", groupname, i);
		add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
	}

	add_rm_user_group(GROUPDEL_BIN, REMOVE, GROUP, groupname, NULL);
}

static int quota_corrupt_test(long isoftlimit, long bsoftlimit, int user_postfix)
{
	int ret, fd;
	long i, j;
	int o_uid, o_gid;
	int user_index;
	struct if_dqblk s_dq, d_dq;
	
	char username[USERNAME_SZ], groupname[GROUPNAME_SZ];

	snprintf(username, USERNAME_SZ, "quotauser-rank%d-%d", rank,
                 user_postfix);
	add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);

	getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
	s_dq.dqb_isoftlimit = isoftlimit;
	s_dq.dqb_ihardlimit = isoftlimit * 2;
	s_dq.dqb_bsoftlimit = bsoftlimit;
	s_dq.dqb_bhardlimit = bsoftlimit * 2;
	s_dq.dqb_curinodes = 0xFFFFFFFFFFFFFFFF;
	s_dq.dqb_curspace = 0xFFFFFFFFFFFFFFFF;
	setquota(QUOTAUSER, device, name2id(USER, username), s_dq);

	o_uid = getuid();
	ret =  seteuid(name2id(USER, username));

	for (i = 0; i < isoftlimit; i++) {
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		
		fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			abort_printf("Open file failed:%d:%s\n", ret, strerror(ret));
		}

		close(fd);
	}

	/*
	 * After a while, we check if the curinodes and curspace 
	 * get synced to be correct.
	*/
	sleep(5);
	seteuid(o_uid);
	getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);

	if (d_dq.dqb_curinodes != isoftlimit)
		abort_printf("Quota corrupt test failed.expected inode_nums = "
			     "%ld, while queried inode_nums = %ld.\n", isoftlimit,
			     d_dq.dqb_curinodes);

	for (i = 0; i < isoftlimit; i++) {
		snprintf(filename, PATH_SZ, "%s/%s-%s-quotafile-%d",
			 workplace, hostname, username, i);
		ret = unlink(filename);
		if (ret < 0) {
			ret = errno;
			abort_printf("Unlink file %s failed:%d:%s.\n",
				     filename, ret, strerror(ret));
		}
	}

	add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
}

static int concurrent_rw_test(long isoftlimit, long bsoftlimit,
			      long user_postfix)
{
	int ret, fd, o_uid, j;
	long i, file_index, writen_size = 0;
	struct if_dqblk s_dq, d_dq;
	char username[USERNAME_SZ];
	char *write_buf;
	
	MPI_Request request;
	MPI_Status status;
	
	if (!rank) {
	
		snprintf(username, USERNAME_SZ, "quota-user-rank%d-%d", rank,
		         user_postfix);
		
		add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);
		getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
		s_dq.dqb_isoftlimit = isoftlimit;
		s_dq.dqb_ihardlimit = isoftlimit * 2;
		s_dq.dqb_bsoftlimit = bsoftlimit;
		s_dq.dqb_bhardlimit = bsoftlimit * 2;
		s_dq.dqb_curinodes = 0;
		s_dq.dqb_curspace = 0;
		setquota(QUOTAUSER, device, name2id(USER, username), s_dq);

	} else
		snprintf(username, USERNAME_SZ, "quota-user-rank0-%d",
			 user_postfix);
	if (!rank) {
		o_uid = getuid();
		seteuid(name2id(USER, username));
		snprintf(filename, PATH_SZ, "%s/%s-quotafile", workplace, username);
		fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
		if (fd < 0) {
			ret = errno;
			abort_printf("Open file failed:%d:%s\n", ret, strerror(ret));
		}
		close(fd);
		seteuid(o_uid);
	}

	MPI_Barrier_Sync();
	
	if (!rank) {
		for (i = 1; i < size; i++) {
			ret = MPI_Irecv(&writen_size, sizeof(long),
					MPI_BYTE, i, 1, MPI_COMM_WORLD, &request);
			if (ret != MPI_SUCCESS)
				abort_printf("MPI_Irecv faile.\n");
			MPI_Wait(&request, &status);
			printf("write_size = %ld, received.\n", writen_size);
			getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);
			if (d_dq.dqb_curspace != writen_size)
				abort_printf("Concurrent test failed among nodes,"
					     "Incorrect space stats found, expected "
					     "space usage = %ld, queried space usage "
					     "= %ld.\n", writen_size, d_dq.dqb_curspace);
		}
	} else {
		snprintf(filename, PATH_SZ, "%s/%s-quotafile", workplace,
			 username);
		fd = open(filename, O_RDWR);
		i = get_rand(0, rank);
		write_buf = (char *)malloc(clustersize * i);
		memset(write_buf, 0, clustersize * i);
		writen_size = pwrite(fd, write_buf, clustersize * i, 0);
		if (writen_size < 0) {
			ret = errno;
			abort_printf("pwrite failed:%d:%s.\n", ret, strerror(ret));
		}
		printf("write_size = %ld, sent.\n", writen_size);
		
		ret = MPI_Isend(&writen_size, sizeof(long), MPI_BYTE, 0, 1,
				MPI_COMM_WORLD, &request);
		if (ret != MPI_SUCCESS)
			abort_printf("MPI_Isend failed.\n");
		MPI_Wait(&request, &status);
		
	}

	MPI_Barrier_Sync();

	if(!rank) {
		ret = unlink(filename);
		if (ret < 0) {
			ret = errno;
			abort_printf("Unlink file failed:%d:%s\n", ret, strerror(ret));
		}

		add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);
	}
}

static int run_tests(void)
{
	int ret;
	int i;
	int fd;
	int o_uid, o_gid;
	struct if_dqblk s_dq, d_dq;
	
	char username[USERNAME_SZ], groupname[GROUPNAME_SZ];

	MPI_Barrier_Sync();
	root_printf("Test %d:Set/Get quota for one user/group among nodes. \n", testno);
	snprintf(username, USERNAME_SZ, "quotauser-%d", rank);
	add_rm_user_group(USERADD_BIN, ADD, USER, username, NULL);

	getquota(QUOTAUSER, device, name2id(USER, username), &s_dq);
	s_dq.dqb_isoftlimit = 10000;
	s_dq.dqb_ihardlimit = 20000;
	s_dq.dqb_bsoftlimit = 1024 * 1024 * 10;
	s_dq.dqb_bhardlimit = 1024 * 1024 * 20;
	setquota(QUOTAUSER, device, name2id(USER, username), s_dq);
	getquota(QUOTAUSER, device, name2id(USER, username), &d_dq);
	verify_quota_items(s_dq, d_dq, ISOFT_LIMIT | IHARD_LIMIT | BSOFT_LIMIT | BHARD_LIMIT);
	add_rm_user_group(USERDEL_BIN, REMOVE, USER, username, NULL);

	snprintf(groupname, GROUPNAME_SZ, "quotagroup-%d", rank);
	add_rm_user_group(GROUPADD_BIN, ADD, GROUP, groupname, NULL);

	getquota(QUOTAGROUP, device, name2id(GROUP, groupname), &s_dq);
	s_dq.dqb_isoftlimit = 20000;
	s_dq.dqb_ihardlimit = 40000;
	s_dq.dqb_bsoftlimit = 1024 * 1024 * 10;
	s_dq.dqb_bhardlimit = 1024 * 1024 * 20;
	setquota(QUOTAGROUP, device, name2id(GROUP, groupname), s_dq);
	getquota(QUOTAGROUP, device, name2id(GROUP, groupname), &d_dq);
	verify_quota_items(s_dq, d_dq, ISOFT_LIMIT | IHARD_LIMIT | BSOFT_LIMIT | BHARD_LIMIT);
	add_rm_user_group(GROUPDEL_BIN, REMOVE, GROUP, groupname, NULL);
	testno++;
	
	MPI_Barrier_Sync();
	root_printf("Test %d:Quota inodes limit test for users/groups among nodes.\n", testno);
	user_inodes_limit_test(100, 1024 * 1024 * 10, 1);
	MPI_Barrier_Sync();
	group_inodes_limit_test(100, 1024 * 1024 * 10, 4, 1);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Quota space limit test for users/groups among nodes.\n", testno);
	user_space_limit_test(100, 1024 * 256, 1);
	MPI_Barrier_Sync();
	group_space_limit_test(100, 1024 * 256, 4, 1);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Quota grace time test among nodes.\n", testno);
	user_inodes_grace_time_test(100, 1024 * 256, 10, 1);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Huge user number test among nodes.\n", testno);
	for (i = 0; i < user_nums; i++)
		user_inodes_limit_test(100, 1024 * 1024 * 2, i);
	testno++;
	
	MPI_Barrier_Sync();
	root_printf("Test %d:Huge group number test among nodes.\n", testno);
	for (i = 0; i < group_nums; i++)
		group_inodes_limit_test(100, 1024 * 1024 * 2, 4, i);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Stress test with intensive quota operations for user/group.\n", testno);
	for (i = 0; i < user_nums; i++) {
		user_inodes_limit_test(100, 1024 * 1024, i);
		MPI_Barrier_Sync();
		group_inodes_limit_test(100, 1024 *1024, 8, i);
	}
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Negative and positive quota test.\n", testno);
	negative_inodes_limit_test(100, 1024 * 1024, 1, 10);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Concurrent file r/w test.\n", testno);
	concurrent_rw_test(100, 1024 * 1024, 1);
	testno++;

	MPI_Barrier_Sync();
	root_printf("Test %d:Quota corruption test.\n", testno);
	quota_corrupt_test(100, 1024 * 1024, 1);
	testno++;

}

static int setup(int argc, char *argv[])
{
	unsigned long i;
	int ret;
	int o_umask;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		log_printf(stderr, "MPI_Init failed: %d\n", ret);
		exit(1);
	}

	if (gethostname(hostname, HOSTNAME_MAX_SZ) < 0)
		abort_printf("get hostname!\n");

	ret = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_rank failed: %d\n", ret);

	ret = MPI_Comm_size(MPI_COMM_WORLD, &size);
	if (ret != MPI_SUCCESS)
		abort_printf("MPI_Comm_size failed: %d\n", ret);


	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	if (parse_opts(argc, argv))
		usage();

	if (geteuid())
		abort_printf("You have to be root to run this program.\n");

	snprintf(workplace, PATH_SZ, "%s/%s", mountpoint, WORKPLACE);
	if (rank == 0) {
		o_umask = umask(0);
		mkdir(workplace, FILE_MODE);
		root_printf("Device:\t\t%s\nMountpoint:\t%s\n"
			    "Iterations:\t\t%d\nUsers:\t\t%d\nGroups:"
			    "\t\t%d\n\n", device, mountpoint,
			    iterations, user_nums, group_nums);
		umask(o_umask);
	}

	open_ocfs2_volume(device);

	MPI_Barrier_Sync();

	quota_on_off(QUOTAON_BIN, 1, QUOTAUSER|QUOTAGROUP, mountpoint);
}

static int teardown(void)
{
	if (rank == 0)
		rmdir(workplace);

	MPI_Barrier_Sync();
	quota_on_off(QUOTAON_BIN, 0, QUOTAUSER|QUOTAGROUP, mountpoint);

	MPI_Finalize();
	return 0;
}

int main(int argc, char *argv[])
{
	int i;

	setup(argc, argv);
	for (i = 0; i < iterations; i++)
		run_tests();

	teardown();
	return 0;
}
