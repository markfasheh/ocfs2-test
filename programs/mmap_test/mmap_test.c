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
/*
 * Description: This is a simple test to see if mmap behaves correctly at 
 *              the end of the file's size.  It first writes up to the 
 *              existing end of the file.  This should work.  Then it writes 
 *              just past the end of the file.  This should error.
 *
 *              This test really has no cluster relevance.
 *
 * Author     : Mark Fasheh
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>


int main(int argc, char *argv[])
{
    char *filename;
    char *ptr, *buf;
    int ret, fd;
    struct stat stat_buf;
    int page_size = getpagesize();
    int offset, remain;


    if ((argc < 2) || (strcmp(argv[1],"-h") == 0))
    {
        fprintf(stderr, "Usage: mmap_test <filename>\n");
        return 1;
    }

    filename = argv[1];

    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror("Open");
        return 1;
    }

    ret = fstat(fd, &stat_buf);
    if (ret)
    {
        perror("Stat");
        return 1;
    }

    buf = mmap(NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED)
    {
        perror("MMap");
        return 1;
    }

    offset = stat_buf.st_size % page_size;
    ptr = buf + (stat_buf.st_size - offset);
    remain = page_size - offset;

    fprintf(stdout, "buf = %p, ptr = %p, size = %lu, offset = %d, remain = %d\n",
            buf, ptr, stat_buf.st_size, offset, remain);

    fprintf(stdout, "tail of ptr: \"");
    fwrite(ptr, sizeof(char), offset, stdout);
    fprintf(stdout, "\"\n");

    fprintf(stdout, "tail of page: \"");
    fwrite(ptr + offset, sizeof(char), remain, stdout);
    fprintf(stdout, "\"\n");

    close(fd);

    munmap(buf, stat_buf.st_size);

    return 0;
}
