/*
 * Verify inline directory data.
 *
 * All tests read back the entire directory to verify correctness.
 *
 * XXX: This could easily be turned into an mpi program, where a
 * second node does the verification step.
 */

#define _XOPEN_SOURCE 600

#include <asm/types.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <dirent.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define OCFS2_MAX_FILENAME_LEN		255

/*
 * OCFS2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define OCFS2_DIR_PAD			4
#define OCFS2_DIR_ROUND			(OCFS2_DIR_PAD - 1)
#define OCFS2_DIR_MEMBER_LEN 		offsetof(struct ocfs2_dir_entry, name)
#define OCFS2_DIR_REC_LEN(name_len)	(((name_len) + OCFS2_DIR_MEMBER_LEN + \
                                          OCFS2_DIR_ROUND) & \
					 ~OCFS2_DIR_ROUND)

/*
 * Quick reference table of namelen boundaries and their respective reclens.
 *
 * (name: 1  rec: 16)      (name: 5  rec: 20)      (name: 9  rec: 24)
 * (name: 13  rec: 28)     (name: 17  rec: 32)     (name: 21  rec: 36)
 * (name: 25  rec: 40)     (name: 29  rec: 44)     (name: 33  rec: 48)
 * (name: 37  rec: 52)     (name: 41  rec: 56)     (name: 45  rec: 60)
 * (name: 49  rec: 64)     (name: 53  rec: 68)     (name: 57  rec: 72)
 * (name: 61  rec: 76)     (name: 65  rec: 80)     (name: 69  rec: 84)
 * (name: 73  rec: 88)     (name: 77  rec: 92)     (name: 81  rec: 96)
 * (name: 85  rec: 100)    (name: 89  rec: 104)    (name: 93  rec: 108)
 * (name: 97  rec: 112)    (name: 101  rec: 116)   (name: 105  rec: 120)
 * (name: 109  rec: 124)   (name: 113  rec: 128)   (name: 117  rec: 132)
 * (name: 121  rec: 136)   (name: 125  rec: 140)   (name: 129  rec: 144)
 * (name: 133  rec: 148)   (name: 137  rec: 152)   (name: 141  rec: 156)
 * (name: 145  rec: 160)   (name: 149  rec: 164)   (name: 153  rec: 168)
 * (name: 157  rec: 172)   (name: 161  rec: 176)   (name: 165  rec: 180)
 * (name: 169  rec: 184)   (name: 173  rec: 188)   (name: 177  rec: 192)
 * (name: 181  rec: 196)   (name: 185  rec: 200)   (name: 189  rec: 204)
 * (name: 193  rec: 208)   (name: 197  rec: 212)   (name: 201  rec: 216)
 * (name: 205  rec: 220)   (name: 209  rec: 224)   (name: 213  rec: 228)
 * (name: 217  rec: 232)   (name: 221  rec: 236)   (name: 225  rec: 240)
 * (name: 229  rec: 244)   (name: 233  rec: 248)   (name: 237  rec: 252)
 * (name: 241  rec: 256)   (name: 245  rec: 260)   (name: 249  rec: 264)
 * (name: 253  rec: 268)
 */

/*
 * OCFS2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define OCFS2_FT_UNKNOWN	0
#define OCFS2_FT_REG_FILE	1
#define OCFS2_FT_DIR		2
#define OCFS2_FT_CHRDEV		3
#define OCFS2_FT_BLKDEV		4
#define OCFS2_FT_FIFO		5
#define OCFS2_FT_SOCK		6
#define OCFS2_FT_SYMLINK	7

#define OCFS2_FT_MAX		8


#define S_SHIFT			12

struct ocfs2_dir_entry {
/*00*/	__u64   inode;                  /* Inode number */
	__u16   rec_len;                /* Directory entry length */
	__u8    name_len;               /* Name length */
	__u8    file_type;
/*0C*/	char    name[OCFS2_MAX_FILENAME_LEN];   /* File name */
/* Actual on-disk length specified by rec_len */
} __attribute__ ((packed));


struct my_dirent {
	unsigned int	type;
	unsigned int	name_len;
	unsigned int	seen;
	char		name[OCFS2_MAX_FILENAME_LEN];
};

#define MAX_DIRENTS	1024
struct my_dirent dirents[MAX_DIRENTS];
static unsigned int num_dirents = 0;

static unsigned int max_inline_size;
unsigned int usable_space;

static unsigned int blocksize;
static char *dir_name;
static char path[PATH_MAX];
static char path1[PATH_MAX];

static int testno = 1;

static unsigned long get_rand(unsigned long min, unsigned long max)
{
	if (min == 0 && max == 0)
		return 0;

	return min + ((rand() % max) - min);
}

static inline char rand_char(void)
{
	return 'A' + (char) get_rand(0, 52);
}

static void usage(void)
{
	printf("Usage: inline-dirs [blocksize] [DIRECTORY]\n"
	       "Run a series of tests intended to verify I/O to and from\n"
	       "files with inline data.\n\n"
	       "blocksize is the blocksize of the underlying file system and\n"
	       "must be specified.\n"
	       "DIRECTORY is the name of a directory which will created and\n"
	       "be used for testing.\n");
}

static int parse_opts(int argc, char **argv)
{
	if (argc < 3)
		return 1;

	blocksize = atoi(argv[1]);
	dir_name = argv[2];

	switch (blocksize) {
	case 4096:
		max_inline_size = 3896;
		break;
	case 2048:
		max_inline_size = 1848;
		break;
	case 1024:
		max_inline_size = 824;
		break;
	case 512:
		max_inline_size = 312;
		break;
	default:
		fprintf(stderr, "Invalid blocksize, %u\n", blocksize);
		return 1;
	}

	usable_space = max_inline_size;
	usable_space -= OCFS2_DIR_REC_LEN(1) + OCFS2_DIR_REC_LEN(2);

	printf("Blocksize:\t\t%d\nMax Inline Data Size:\t%d\nDir Name:\t\t%s\n"
	       "Usable Dir Space:\t%d\n",
	       blocksize, max_inline_size, dir_name, usable_space);

	return 0;
}

static int is_dot_entry(struct my_dirent *dirent)
{
	if (dirent->name_len == 1 && dirent->name[0] == '.')
		return 1;
	if (dirent->name_len == 2 && dirent->name[0] == '.'
	    && dirent->name[1] == '.')
		return 1;
	return 0;
}

static int unlink_dirent(struct my_dirent *dirent)
{
	sprintf(path1, "%s/%s", dir_name, dirent->name);

	return unlink(path1);
}

static void destroy_dir(void)
{
	int ret, i;
	struct my_dirent *dirent;

	for(i = 0; i < num_dirents; i++) {
		dirent = &dirents[i];

		if (dirent->name_len == 0)
			continue;

		if (!is_dot_entry(dirent)) {
			ret = unlink_dirent(dirent);
			if (ret) {
				ret = errno;
				fprintf(stderr, "unlink failure %d: %s\n", ret,
					strerror(ret));
				exit(ret);
			}
		}

		dirent->name_len = 0;
	}

	ret = rmdir(dir_name);
	if (ret) {
		ret = errno;
		fprintf(stderr, "rmdir failure %d: %s\n", ret,
			strerror(ret));
		exit(ret);
	}
}

static struct my_dirent *find_my_dirent(char *name)
{
	int i, len;
	struct my_dirent *my_dirent;

	len = strlen(name);

	for(i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->name_len == 0)
			continue;

		if (my_dirent->name_len == len &&
		    strcmp(my_dirent->name, name) == 0)
			return my_dirent;
	}

	return NULL;
}

static void create_and_prep_dir(void)
{
	int ret;
	struct my_dirent *dirent;

	memset(dirents, 0, sizeof(dirents));

	dirent = &dirents[0];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 1;
	strcpy(dirent->name, ".");

	dirent = &dirents[1];
	dirent->type = S_IFDIR >> S_SHIFT;
	dirent->name_len = 2;
	strcpy(dirent->name, "..");

	num_dirents = 2;

	ret = mkdir(dir_name, 0755);
	if (ret) {
		ret = errno;
		fprintf(stderr, "mkdir failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}
}

static void create_file(char *filename)
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

	fd = open(path, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd == -1) {
		ret = errno;
		fprintf(stderr, "open failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}

	close(fd);
}

static void create_files(char *prefix, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		sprintf(path1, "%s%011d", prefix, i);
		create_file(path1);
	}
}

static void get_directory_almost_full(int minus_this_many)
{
	unsigned int almost_full_entries;

	/*
	 * This will create enough entries to leave only 280 free
	 * bytes in the directory.
	 *
	 * max_inline_size % 512 = 312    [always]
	 * rec overhead for '.' and '..' = 32
	 * So, 312 - 32 = 280.
	 *
	 */
	almost_full_entries = max_inline_size / 512;
	almost_full_entries *= 512;
	almost_full_entries /= 32;

	/*
	 * Now we add enough 32 byte entries to fill that remaining 280 bytes:
	 *
	 * 280 / 32 = 8
	 *
	 * And we'll be left over with 24 bytes:
	 *
	 * 280 % 32 = 24
	 *
	 * Which can easily be overflowed by adding one more 32 byte entry.
	 */
	almost_full_entries += 8;
	almost_full_entries -= minus_this_many;

	/* Need up to 20 characters to get a 32 byte entry */
	create_files("filename-", almost_full_entries);
}

static void random_unlink(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters--) {
		i = get_rand(0, num_dirents);
		if (i >= num_dirents)
			abort();
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		ret = unlink_dirent(dirent);
		if (ret) {
			ret = errno;
			fprintf(stderr, "unlink failure %d: %s\n", ret,
				strerror(ret));
			exit(ret);
		}

		dirent->name_len = 0;
	}
}

static void random_rename_same_reclen(int iters)
{
	int i, ret;
	struct my_dirent *dirent;

	while (iters--) {
		i = get_rand(0, num_dirents);
		if (i >= num_dirents)
			abort();
		dirent = &dirents[i];

		if (is_dot_entry(dirent))
			continue;
		if (dirent->name_len == 0)
			continue;

		/*
		 * We already renamed this one
		 */
		if (dirent->name[0] == 'R')
			continue;

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
	}
}

static void random_deleting_rename(int iters)
{
	int i, j, ret;
	struct my_dirent *dirent1, *dirent2;

	while (iters--) {
		i = get_rand(0, num_dirents);
		if (i >= num_dirents)
			abort();
		j = get_rand(0, num_dirents);
		if (j >= num_dirents)
			abort();
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
	}
}

static void verify_dirents(void)
{
	int i, ret;
	DIR *dir;
	struct dirent *dirent;
	struct my_dirent *my_dirent;

	dir = opendir(dir_name);
	if (dir == NULL) {
		ret = errno;
		fprintf(stderr, "opendir failure %d: %s\n", ret, strerror(ret));
		exit(ret);
	}

	dirent = readdir(dir);
	while (dirent) {
		my_dirent = find_my_dirent(dirent->d_name);
		if (!my_dirent) {
			fprintf(stderr, "Verify failure: got nonexistent "
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

		dirent = readdir(dir);
	}

	for (i = 0; i < num_dirents; i++) {
		my_dirent = &dirents[i];

		if (my_dirent->seen != 0 || my_dirent->name_len == 0)
			continue;

		fprintf(stderr, "Verify failure: missing dirent: "
			"(type: %u, name_len: %u, name: %s)\n", my_dirent->type,
			my_dirent->name_len, my_dirent->name);
		exit(1);
	}
	closedir(dir);
}

/*
 * [I] Basic tests of inline-dir code.
 *    1) Basic add files
 *    2) Basic delete files
 *    3) Basic rename files
 *    4) Add / Remove / Re-add to fragment dir
 */
static void run_basic_tests(void)
{
	printf("Test %d: fill directory\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	verify_dirents();
	testno++;

	printf("Test %d: remove directory\n", testno);
	destroy_dir();
	testno++;

	printf("Test %d: rename files with same namelen\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_rename_same_reclen(20);
	verify_dirents();
	destroy_dir();
	testno++;

	printf("Test %d: rename files with same namelen on top of each other\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_deleting_rename(20);
	verify_dirents();
	destroy_dir();
	testno++;

	printf("Test %d: random rename/unlink files with same namelen\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(20);
	random_rename_same_reclen(20);
	verify_dirents();
	destroy_dir();
	testno++;

	printf("Test %d: fragment directory with unlinks/creates/renames\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(20);
	random_rename_same_reclen(20);
	create_files("frag1a", 20);
	random_unlink(20);
	create_files("frag1b", 20);
	random_deleting_rename(20);
	random_rename_same_reclen(20);
	create_files("frag1c", 20);
	verify_dirents();
	destroy_dir();
	testno++;
}

/*
 * [II] Tests intended to push a dir out to extents
 *    1) Add enough files to push out one block
 *    2) Add enough files to push out two blocks
 *    3) Fragment dir, add enough files to push out to extents
 */
static void run_large_dir_tests(void)
{
	printf("Test %d: Add file name large enough to push out one block\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	create_files("Pushedfn-", 1);
	verify_dirents();
	destroy_dir();
	testno++;

	printf("Test %d: Add file name large enough to push out two blocks\n", testno);
	create_and_prep_dir();
	get_directory_almost_full(0);
	create_files("this_is_an_intentionally_long_filename_prefix_to_stress_the_dir_code-this_is_an_intentionally_long_filename_prefix_to_stress_the_dir_code-this_is_an_intentionally_long_filename_prefix_to_stress_the_dir_code", 1);
	verify_dirents();
	destroy_dir();
	testno++;

	printf("Test %d: fragment directory then push out to extents.\n",
	       testno);
	create_and_prep_dir();
	get_directory_almost_full(1);
	random_unlink(20);
	random_rename_same_reclen(20);
	create_files("frag2a", 20);
	random_unlink(20);
	create_files("frag2b", 20);
	random_deleting_rename(20);
	random_rename_same_reclen(20);
	create_files("frag2c", 30);
	verify_dirents();
	destroy_dir();
	testno++;
}

int main(int argc, char **argv)
{
	if (parse_opts(argc, argv)) {
		usage();
		return EINVAL;
	}

	srand(getpid());

	run_basic_tests();

	run_large_dir_tests();

	printf("All File I/O Tests Passed\n");

	return 0;
}
