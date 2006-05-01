/******************************************************************************
*******************************************************************************
**
**    Copyright 2004 Intel, Inc.
**
**    This is free software released under the GNU General Public License.
**    There is no warranty for this software.  See the file COPYING for
**    details.
**
**
**    This file is maintained by:
**      LingXiaofeng <xiaofeng.ling@intel.com>
**      Aaron,Chen <yukun.chen@intel.com>
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define NORMAL_READ_WRITE    1
#define MEMORYMAP_READ_WRITE 2
#define FALSE 0
#define TRUE  1

char filename[256];

#define SIZE 1024 * 4
int bufsize = SIZE;
char buf1[SIZE+1];
char buf2[SIZE+1];

help()
{
 	printf("usage: <-is>\n"
        "-i <node> 0-this node, 1-other node\n"
        "-s <num> size of create file, num * bufsize\n"
        "-w <type> the way of writing to the file. 1 means a normal read/write, 2 means a memorymap way\n"
        "-v <type> the way of verify the previous is ok. 1 means a normal read/write, 2 means a memorymap way\n"
	);
   
}

int node = 0;
int size = 512;
int mode = 0;
int do_type = NORMAL_READ_WRITE;

getfilename()
{
	char *ocfsroot;

	ocfsroot = getenv("OCFSROOT");
	if(ocfsroot == NULL)
			ocfsroot = "/ocfs2";

	snprintf(filename, 255, "%s/ocfs_locktest", ocfsroot);
	filename[255] = 0;
}

void  writefile(char *filename, int node, int size)
{

	int fd, i, c;
	int err;	

	memset(buf1, 'A' + node, bufsize);

	fd = open(filename, O_CREAT | O_RDWR );
	if (fd < 0) {
		fprintf(stderr, " can't open file %s: %s\n",
			filename, strerror(errno));
		exit(EXIT_FAILURE);
	}
	i = node;	
	err = lseek(fd, bufsize * i, SEEK_SET);	
	if(err == -1) {
		fprintf(stderr, "lseek fail\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	while(i < size) {
		write(fd, buf1, bufsize);
		i+=2;
		lseek(fd, bufsize * i, SEEK_SET);
	}
	close(fd);
	
	
}

int verify(char *filename, int node,int size)
{
	int fd, i, c;
	int err;	

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, " can't open file %s: %s\n",
			filename, strerror(errno));
		exit(EXIT_FAILURE);
	}
	i = node;	
	err = lseek(fd, bufsize * i, SEEK_SET);
	if(err == -1) {
		fprintf(stderr, "lseek fail\n");
		close(fd);
		exit(EXIT_FAILURE);
	}

	memset(buf1, 'A' + node, bufsize);
	while(i < size) {
		read(fd, buf2, bufsize);
		err = memcmp(buf1, buf2, bufsize);
		if(err != 0) {
			fprintf(stderr, "read and write not match! %d\n", i);
			close(fd);
			return EXIT_FAILURE;
		}
		i+=2;
		lseek(fd, bufsize * i, SEEK_SET);
	}
	close(fd);
	return EXIT_SUCCESS;
}

void writefile_mm(char *filename, int node, int size)
{

	int fd, i, c;
	int err;	
	char *p;

	if ( size < 2 ){
		fprintf(stderr, "size must be larger than 2");
		exit(EXIT_FAILURE);
	}

	memset(buf1, 'A' + node, bufsize);

	fd = open(filename, O_CREAT | O_RDWR );
	if (fd < 0) {
		fprintf(stderr, " can't open file %s: %s\n",
			filename, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	/*debug only*/
	memset(buf2, 'C' + node, bufsize);
	i = 0;
	err = lseek(fd, bufsize * i, SEEK_SET);	
	if(err == -1) {
		fprintf(stderr, "lseek fail\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i< size; i++) write(fd, buf2, bufsize);
	/*debug only*/

	i = node;	
  	p = (char *) mmap(NULL, bufsize, PROT_READ | PROT_WRITE, MAP_SHARED,
			   fd, bufsize * i);
  	if (p == MAP_FAILED) {
  	      close (fd);
	      perror("mmap error:");
	      exit(EXIT_FAILURE);
        }


	while(i < size-2) {

	  	memcpy(p, buf1, bufsize);
   	        munmap(p, bufsize);

		i+=2;
		p = (char *) mmap(NULL, bufsize, PROT_READ | PROT_WRITE, MAP_SHARED,
			   fd, bufsize * i);
		if (p == MAP_FAILED) {
		  close (fd);
		  perror("mmap error:");
		  exit(EXIT_FAILURE);
		}
	}

  	memcpy(p, buf1, bufsize);
	munmap(p, bufsize);
	
	close(fd);
}

int  verify_mm(char *filename, int node,int size)
{
	int fd, i, c;
	int err;	
	char *p;

	if ( size < 2 ){
		fprintf(stderr, "size must be larger than 2");
		exit(EXIT_FAILURE);
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, " can't open file %s: %s\n",
			filename, strerror(errno));
		exit(EXIT_FAILURE);
	}

	memset(buf1, 'A' + node, bufsize);
	i = node;
  	p = (char *) mmap(NULL, bufsize, PROT_READ, MAP_SHARED,
			   fd, bufsize * i);
  	if (p == MAP_FAILED) {
  	      close (fd);
	      perror("mmap error:");
	      exit(EXIT_FAILURE);
        }

	while(i < size-2) {
		err = memcmp(buf1, p, bufsize);
		if(err != 0) {
			fprintf(stderr, "read and write not match! %d\n", i);
			close(fd);
			return EXIT_FAILURE;
		}


		i+=2;
   	        munmap(p, bufsize);
	  	p = (char *) mmap(NULL, bufsize, PROT_READ, MAP_SHARED,
			   fd, bufsize * i);
		if (p == MAP_FAILED) {
		  close (fd);
		  perror("mmap error:");
		  exit(EXIT_FAILURE);
		}
	}

	err = memcmp(buf1, p, bufsize);
	if(err != 0) {
		fprintf(stderr, "read and write not match! %d\n", i);
		close(fd);
		return EXIT_FAILURE;
	}

	munmap(p, bufsize);
	close(fd);
	return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{
	int fd, i, c;
	int err;	
	void (*write_fun)(char *, int, int) = writefile;
	int (*verify_fun)(char *, int, int) = verify;
	int found_w=FALSE, found_v=FALSE;

	getfilename();
	while ((c = getopt(argc, argv, "i:s:r:w:v:")) != EOF)
    {
        switch (c)
        {
        case 'i':
            node = atoi(optarg);
			if(node != 0 && node != 1)
			{
				printf("node number can only be 0 and 1");
				exit(1);
			}
            break;

        case 's':
            size = atoi(optarg);
            break;

	case 'w':
	        found_w=TRUE;
	        do_type = atoi(optarg);
	        if ( do_type == NORMAL_READ_WRITE )
		     write_fun = writefile;
		else if ( do_type == MEMORYMAP_READ_WRITE)
		     write_fun = writefile_mm;
		else{
		     printf("pls input an valid way fo writing file. Only 1 or 2 is allowed to use");
		     exit(1);
		}  
		    
		break;

	case 'v':
 	        found_v=TRUE;
	        do_type = atoi(optarg);
	        if ( do_type == NORMAL_READ_WRITE )
		     verify_fun = verify;
		else if ( do_type == MEMORYMAP_READ_WRITE)
		     verify_fun = verify_mm;
		else{
		     printf("pls input an valid way fo writing file. Only 1 or 2 is allowed to use");
		     exit(1);
		}  
		    
		break;	  
        case 'h':
        default:
            help();
            exit(1);
        }/*switch*/
    }/*while*/

        if ( found_w )
	  (*write_fun)(filename, node, size);
	if ( found_v )
	  err = (*verify_fun)(filename, node, size);		

	return (err);
}


