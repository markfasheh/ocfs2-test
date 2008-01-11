/*
 * test_remove.c
 *
 * test file for removing slots.
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
 * This file is used to create some boudary situation to test whether
 * tunefs.ocfs2 can work OK with removing slots.
 *
 * When we have orphan files or have some blocks allocated in truncate log
 * or local alloc, we can't remove the slots, so the option CREATE_ORPHAN_FILE,
 * CREATE_TRUNCATE_LOG and CREATE_LOCAL_ALLOC are used to check it.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <libgen.h>
#include <stddef.h>

#include <ocfs2/ocfs2.h>
#include <ocfs2/byteorder.h>


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
	CREATE_TRUNCATE_LOG = 1,
	CREATE_LOCAL_ALLOC,
	CREATE_ORPHAN_FILE
};

struct{
	uint16_t slot;
	enum operations ops;
} options;

static char *device = NULL;

static errcode_t create_local_alloc(ocfs2_filesys *fs, uint16_t slot);
static errcode_t create_orphan_file(ocfs2_filesys *fs, uint16_t slot);
static errcode_t create_truncate_log(ocfs2_filesys *fs, uint16_t slot);

static void usage (const char *progname)
{
	fprintf(stderr, "usage: %s -n node-num [-lot] device\n",
		progname);

	exit(0);
}

static int read_options(int argc, char **argv)
{
	int c;

	progname = basename(argv[0]);

	if (argc < 2)
		return 1;

	while(1) {
		c = getopt(argc, argv, "n:lot");
		if (c == -1)
			break;

		switch (c) {
		case 'n':	/* the slot num. */
			options.slot = strtoul(optarg, NULL, 0);
			break;

		case 'l':
			if (options.ops != 0)
				usage(progname);
			options.ops = CREATE_LOCAL_ALLOC;
			break;

		case 'o':
			if (options.ops != 0)
				usage(progname);
			options.ops = CREATE_ORPHAN_FILE;
			break;

		case 't':
			if (options.ops != 0)
				usage(progname);
			options.ops = CREATE_TRUNCATE_LOG;
			break;

		default:
			usage(progname);
			break;
		}
	}

	if (optind < argc && argv[optind])
		device = argv[optind];

	return 0;
}

int main (int argc, char **argv)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 1;

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

	if (!device || !options.ops)
		goto bail;

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	srand((unsigned long)fs);

	switch (options.ops) {
		case CREATE_LOCAL_ALLOC:
			ret = create_local_alloc(fs, options.slot);
			break;

		case CREATE_ORPHAN_FILE:
			ret = create_orphan_file(fs, options.slot);
			break;

		case CREATE_TRUNCATE_LOG:
			ret = create_truncate_log(fs, options.slot);
			break;
	}
bail:
	if (fs)
		ocfs2_close(fs);

	return ret;
}

static inline uint32_t get_local_alloc_window_bits()
{
	/* just return a specific number for test */
	return 256;
}

static errcode_t create_local_alloc(ocfs2_filesys *fs, uint16_t slot)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_local_alloc *la;
	uint32_t la_size, found;
	uint64_t la_off, blkno;

	ret = ocfs2_lookup_system_inode(fs, LOCAL_ALLOC_SYSTEM_INODE,
					slot, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		goto bail;

	if (!(di->i_flags & OCFS2_LOCAL_ALLOC_FL))
		goto bail;

	if (di->id1.bitmap1.i_total > 0) {
		fprintf(stderr, "local alloc#%"PRIu64" file not empty."
			"Can't create a new one.\n", blkno);
		goto bail;
	}

	la_size = get_local_alloc_window_bits();

	ret = ocfs2_new_clusters(fs, 1, la_size, &la_off, &found);
	if (ret)
		goto bail;

	if(la_size != found)
		goto bail;

	la = &(di->id2.i_lab);

	la->la_bm_off = ocfs2_blocks_to_clusters(fs, la_off);
	di->id1.bitmap1.i_total = la_size;
	di->id1.bitmap1.i_used = 0;
	memset(la->la_bitmap, 0, la->la_size);
	
	ret = ocfs2_write_inode(fs, blkno, buf);

bail:
	if(buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t create_orphan_file(ocfs2_filesys *fs, uint16_t slot)
{
	errcode_t ret;
	uint64_t dir, tmp_blkno;
	char name[OCFS2_MAX_FILENAME_LEN];
	int namelen;

	ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
					slot, &dir);
	if (ret)
		return ret;

	namelen = sprintf(name, "test%ld", random());

	ret = ocfs2_lookup(fs, dir, name, namelen, NULL, &tmp_blkno);
	if (!ret)
		return 0;
	else if (ret != OCFS2_ET_FILE_NOT_FOUND)
		return ret;

	ret = ocfs2_new_inode(fs, &tmp_blkno, S_IFREG | 0755);
	if (ret)
		return ret;

	ret = ocfs2_link(fs, dir, name,
			 tmp_blkno, OCFS2_FT_REG_FILE);
	if (ret == OCFS2_ET_DIR_NO_SPACE) {
		ret = ocfs2_expand_dir(fs, dir, fs->fs_root_blkno);
		if (ret)
			return ret;

		ret = ocfs2_link(fs, dir, name,
				 tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			return ret;
	} else if (ret)
		return ret;

	return 0;
}

static errcode_t create_truncate_log(ocfs2_filesys *fs, uint16_t slot)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;
	uint16_t i, used = 10;
	uint32_t found, clusters = 10;
	uint64_t begin, blkno;

	ret = ocfs2_lookup_system_inode(fs, TRUNCATE_LOG_SYSTEM_INODE,
					slot, &blkno);
	if (ret)
		goto bail;
	
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL)) {
		fprintf(stderr,"not a valid file\n");
		goto bail;
	}

	if (!(di->i_flags & OCFS2_DEALLOC_FL)) {
		fprintf(stderr,"not a valid truncate log\n");
		goto bail;
	}
	
	tl = &di->id2.i_dealloc;

	if (le16_to_cpu(tl->tl_used) > 0) {
		fprintf(stderr,"truncate log#%"PRIu64" file not empty."
			"Can't create a new one.\n", blkno);
		goto bail;
	}

	tl->tl_used = used;

	for (i = 0; i < tl->tl_used; i++) {
		ret = ocfs2_new_clusters(fs, 1, clusters, &begin, &found);
		if (ret)
			goto bail;
		
		tl->tl_recs[i].t_start = 
			ocfs2_blocks_to_clusters(fs, begin);
		tl->tl_recs[i].t_clusters = found;
	}

	ret = ocfs2_write_inode(fs, blkno, buf);

bail:
	if(buf)
		ocfs2_free(&buf);
	return ret;
}
