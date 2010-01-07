/*
 * test_truncate.c
 *
 * test file for ocfs2_truncate
 *
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
 *
 */

/*
 * This file is used to test whether ocfs2_truncate can truncate
 * a file to a specified size.
 *
 * An additional option "c" is to create a file before we test truncating.
 * We can give the tree depth so that the file can be created with the
 * specified tree depth.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <libgen.h>
#include <stddef.h>

#include <ocfs2/ocfs2.h>


char *progname = NULL;

static void handle_signal (int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		exit(1);
	}

	return ;
}

enum operations {
	CREATE = 1,
	SET_SIZE
};

struct{
	char file_name[OCFS2_MAX_FILENAME_LEN];
	enum operations ops;
	uint64_t extras;
} options;

static char *device = NULL;

static void usage (const char *progname)
{
	fprintf(stderr, "usage: %s -f file-name\n"
		"\t\t[-c tree-depth] [-s new-size] device\n",
		progname);

	exit(0);
}

static int read_options(int argc, char **argv)
{
	int c;

	progname = basename(argv[0]);

	if ((argc < 2) || (strcmp(argv[1],"-h") == 0))
		return 1;

	while(1) {
		c = getopt(argc, argv, "f:c:s:");
		if (c == -1)
			break;

		switch (c) {
		case 'f':	/* file name */
			strcpy(options.file_name, optarg);
			break;

		case 'c':
			if (options.ops != 0)
				usage(progname);
			options.ops = CREATE;
			options.extras = strtoull(optarg, NULL, 0);
			if (options.extras > 4) {
				com_err(progname, 0, "We can only create"
					"a file with tree_depth <= 4");
				exit(1);
			}
			break;

		case 's':
			if (options.ops != 0)
				usage(progname);
			options.ops = SET_SIZE;
			options.extras = strtoull(optarg, NULL, 0);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		device = argv[optind];

	return 0;
}

static errcode_t open_test_inode(ocfs2_filesys *fs, char *name, uint64_t *ino)
{
	errcode_t ret = 0;
	uint64_t tmp_blkno = 0;
	int namelen = strlen(name);

	ret = ocfs2_lookup(fs, fs->fs_root_blkno, name, namelen,
			   NULL, &tmp_blkno);
	if (!ret) {
		*ino = tmp_blkno;
		return 0;
	} else if (ret != OCFS2_ET_FILE_NOT_FOUND)
		return ret;

	ret = ocfs2_new_inode(fs, &tmp_blkno, S_IFREG | 0755);
	if (ret)
		return ret;

	ret = ocfs2_link(fs, fs->fs_root_blkno, name,
			 tmp_blkno, OCFS2_FT_REG_FILE);
	if (ret)
		return ret;

	*ino = tmp_blkno;

	return 0;
}

/*
 * This function is similar to ocfs2_extend_allocation() as both extend files.
 * However, this one ensures that the extent record tree grows much faster.
 */
static errcode_t custom_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
					  uint32_t new_clusters)
{
	errcode_t ret;
	uint32_t n_clusters;
	uint32_t i, offset = 0;
	uint64_t blkno;
	uint64_t tmpblk;

	while (new_clusters) {
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			goto bail;

		/* In order to ensure the extent records are not coalesced,
		 * we insert each cluster in reverse. */
		for(i = n_clusters; i; --i) {
			tmpblk = blkno + ocfs2_clusters_to_blocks(fs, i - 1);
		 	ret = ocfs2_inode_insert_extent(fs, ino, offset++,
							tmpblk, 1, 0);
			if (ret) 
				goto bail;	
		}
	 	new_clusters -= n_clusters;
	}

bail:
	return ret;
}

static inline int get_rand(int min, int max)
{
	if (min == max)
		return min;

	return min + (rand() % (max - min));
}

/* Create the file with the specified tree_depth. */
static errcode_t create_file(ocfs2_filesys *fs, uint64_t ino, uint64_t tree_depth)
{
	errcode_t ret;
	uint64_t size;
	char *buf = NULL;
	uint32_t clusters = 0;
	struct ocfs2_dinode *di = NULL;
	int random_rec_in_last_eb, random_rec_in_dinode;
	int ext_rec_per_inode = ocfs2_extent_recs_per_inode(fs->fs_blocksize);
	int ext_rec_per_eb = ocfs2_extent_recs_per_eb(fs->fs_blocksize);

	srand((unsigned int)fs);
	/* In order to build up the tree quickly, we allocate only 1 cluster
	 * to an extent, so we can calculate the extent numbers and set the
	 * clusters accordingly.
	 */
	random_rec_in_dinode = get_rand(1, ext_rec_per_inode);

	if (tree_depth == 0)
		clusters = random_rec_in_dinode;
	else {
		/* Now we will create a tree with the tree_depth.
		 * we will increase the tree detph gradually.
		 * 
		 * In order to speed up the generation, we just set
		 * the tree root to be "3"(there are only 2 extent rec
		 * in the dinode which are full.
		 */
		clusters = 2;
		while (tree_depth--) {
			clusters *= ext_rec_per_eb;
			random_rec_in_last_eb = get_rand(1, ext_rec_per_eb);
			clusters += random_rec_in_last_eb;
		}
	}
	
	if (clusters == 0)
		return -1;

	ret = custom_extend_allocation(fs, ino, clusters);
	if (ret)
		goto bail;

	/* set the file size accordingly since ocfs2_truncate will
	 * check the size during its operation.
	 */
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;
	
	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto bail;

	size = clusters * fs->fs_clustersize;
	di = (struct ocfs2_dinode *)buf;
	di->i_size = size;

	ret = ocfs2_write_inode(fs, ino, buf);	

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

int main (int argc, char **argv)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 1;
	uint64_t inode;

	initialize_ocfs_error_table();

#define INSTALL_SIGNAL(sig)					\
	do {							\
		if (signal(sig, handle_signal) == SIG_ERR) {	\
		    printf("Could not set " #sig "\n");		\
		    goto bail;					\
		}						\
	} while (0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	memset(&options, 0, sizeof(options));
	if (read_options(argc, argv)) {
		usage(progname);
		goto bail;
	}

	if (!device || !options.ops || !options.file_name[0])
		goto bail;

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	ret = open_test_inode(fs, options.file_name, &inode);
	if (ret) {
		com_err(progname, ret, "while open test inode");
		goto bail;
	}

	if (options.ops == CREATE)
		ret = create_file(fs, inode, options.extras);
	else
		ret = ocfs2_truncate(fs, inode, options.extras);

	if (ret)
		com_err(progname, ret, "while doing the test");
bail:
	if (fs)
		ocfs2_close(fs);

	return ret;
}
