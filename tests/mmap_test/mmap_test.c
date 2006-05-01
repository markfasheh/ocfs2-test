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


    if (argc < 2)
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
