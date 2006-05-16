/*
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mpi.h"

struct run_item {
	char ri_name[NAME_MAX + 1];
	struct run_item *ri_next;
};

static int run_executible(char *execpath, int argc, char *argv[])
{
	char **args;
	int numargs, i, ret;
	pid_t pid;

	numargs = argc + 1;
	args = calloc(numargs + 1, sizeof(char *));
	if (!args)
		return ENOMEM;

	args[numargs] = NULL;
	args[0] = execpath;
	for (i = 1; i < numargs; i++)
		args[i] = argv[i - 1];

	pid = fork();
	if (pid == -1) {
		ret = errno;
		goto out;
	}

	if (pid) {
		ret = waitpid(pid, NULL, 0);
		if (ret == -1) {
			ret = errno;
			goto out;
		}
		ret = 0;
	} else {
		ret = execv(execpath, args);
		if (ret) {
			ret = errno;
			fprintf(stderr, "Exec error %d\n", ret);
		}
		exit(ret);
	}

out:
	free(args);
	return ret;
}

static int is_runnable(const char *path)
{
	int ret;
	struct stat st;

	ret = stat(path, &st);
	if (ret) {
		ret = errno;
		fprintf(stderr,
			"Error %d while trying to stat %s\n", ret, path);
		return 0;
	}

	if (!S_ISREG(st.st_mode))
		return 0;

	/* R_OK so we can run scripts */
	return !access(path, R_OK|X_OK);
}

static int run_item_list(struct run_item *items, int argc, char *argv[])
{
	int ret = 0;

	while (items) {
		ret = run_executible(items->ri_name, argc, argv);
		if (ret) {
			fprintf(stderr,
				"Error %d running \"%s\"\n", ret, items->ri_name);
			break;
		}

		ret = MPI_Barrier(MPI_COMM_WORLD);
		if (ret != MPI_SUCCESS) {
			fprintf(stderr,
				"Error %d during MPI_Barrier()\n", ret);
			break;
		}

		items = items->ri_next;
	}

	return ret;
}

static char *skippable_tails[] = { "~",
				   ".rpmsave",
				   ".rpmorig",
				   ".rpmnew",
				   ".swp",
				   ",v",
				   NULL};

static int should_not_exec(const char *path)
{
	int pathlen = strlen(path);
	int len;
	char **tail;

	tail = skippable_tails;
	while (*tail) {
		len = strlen(*tail);
		if (!strcmp(*tail, &path[pathlen - len]))
			return 1;

		tail++;
	}
	return 0;
}

static int run_filter(const struct dirent *dirent)
{
	if (!strcmp(".", dirent->d_name) ||
	    !strcmp("..", dirent->d_name))
		return 0;

	if (should_not_exec(dirent->d_name))
		return 0;

	return 1;
}

/*
 * This function expects that *first == NULL.
 *
 * XXX: Technically, there is no need for a linked list, so perhaps we
 * just allocate an array in the future?
 */
static int build_item_list(const char *dirpath, struct run_item **first)
{
	int ret, num, i;
	char path[PATH_MAX + 1];
	struct dirent *dirent;
	struct dirent **namelist;
	struct run_item *item;

	num = scandir(dirpath, &namelist, run_filter, alphasort);
	if (num == -1)
		return errno;
	if (num == 0)
		return 0;

	ret = 0;
	for(i = num - 1; i >= 0; i--) {
		dirent = namelist[i];

		snprintf(path, PATH_MAX + 1, "%s/%s", dirpath, dirent->d_name);

		if (!is_runnable(path))
			continue;

		item = calloc(1, sizeof(struct run_item));
		if (!item) {
			ret = ENOMEM;
			break;
		}

		strcpy(item->ri_name, path);
		item->ri_next = *first;

		*first = item;
	}

	while(num--)
		free(namelist[num]);
	free(namelist);

	return ret;
}

static void usage(void)
{
	printf("usage: mpi-run-parts <part> [ args ... ]\n"
	       "If <part> is a file it will be run, passing [ args ... ]\n"
	       "in as arguments.\n"
	       "If <part> is a directory, each executible in that directory\n"
	       "is run, passing [ args ... ] in as arguments.\n"
	       "A barrier is waited on after each execution, so that programs\n"
	       "are run in lockstep within the MPI domain.\n");
}

int main(int argc, char *argv[])
{
	int ret, exec_argc, len;
	char path[PATH_MAX + 1];
	char **exec_argv = NULL;
	struct run_item *item_list = NULL;
	struct run_item *tmp_item;

	ret = MPI_Init(&argc, &argv);
	if (ret != MPI_SUCCESS) {
		fprintf(stderr, "MPI_Init failed: %d\n", ret);
		return ret;
	}

	if (argc < 2) {
		usage();
		ret = 1;
		goto out;
	}

	len = strlen(argv[1]);
	if (len > PATH_MAX) {
		fprintf(stderr, "Path \"%s\" is too long\n", argv[1]);
		ret = ENAMETOOLONG;
		goto out;
	}

	strncpy(path, argv[1], PATH_MAX + 1);

	while (len && path[--len] == '/')
		path[len] = '\0';

	exec_argc = argc - 2;
	if (exec_argc)
		exec_argv = &argv[2];

	if (is_runnable(path)) {
		ret = run_executible(path, exec_argc, exec_argv);
		if (ret)
			fprintf(stderr, "Error %d executing %s\n", ret, path);
		goto out;
	}

	ret = build_item_list(path, &item_list);
	if (ret) {
		fprintf(stderr, "Error %d reading directory %s\n", ret, path);
		goto out;
	}

	ret = run_item_list(item_list, exec_argc, exec_argv);
	if (ret)
		fprintf(stderr, "Error %d executing from directory %s\n", ret,
			path);

out:
	while (item_list) {
		tmp_item = item_list;
		item_list = item_list->ri_next;
		free(tmp_item);
	}

	if (ret)
		MPI_Abort(MPI_COMM_WORLD, 1);
	else
		MPI_Finalize();

	return ret;
}
