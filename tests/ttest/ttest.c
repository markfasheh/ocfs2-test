#define _LARGEFILE64_SOURCE
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>


static int get_number(char *arg, uint64_t *res)
{
    char *ptr = NULL;
    uint64_t num;

    num = strtoull(arg, &ptr, 0);
    if ((ptr == arg) || 
        (num == UINT64_MAX))
        return(-EINVAL);

    switch (*ptr)
    {
        case '\0':
            break;

        case 'g':
        case 'G':
            num *= 1024;
            /* FALL THROUGH */

        case 'm':
        case 'M':
            num *= 1024;
            /* FALL THROUGH */

        case 'k':
        case 'K':
            num *= 1024;
            /* FALL THROUGH */

        case 'b':
        case 'B':
            break;

        default:
            return(-EINVAL);
    }

    *res = num;
    return(0);
}

static void _err(int _en, char *str)
{
    fprintf(stderr, "%s: %s\n", str, strerror(_en));
}

static void print_usage(void)
{
    fprintf(stderr, "Usage: ttest [-t <trunc_to>] [-w <write_at>] <filename>\n");
}

int main(int argc, char *argv[])
{
    uint64_t trunc_to = 0, write_at = 0;
    int c, ret, fd;
    int trunc = 0;
    char *filename;

    while ((c = getopt(argc, argv, "t:w:")) != EOF)
    {
        switch (c)
        {
            case 't':
                ret = get_number(optarg, &trunc_to);
                if (ret)
                {
                    print_usage();
                    return 1;
                }
                trunc = 1;
                break;

            case 'w':
                ret = get_number(optarg, &write_at);
                if (ret)
                {
                    print_usage();
                    return 1;
                }
                break;

            default:
                print_usage();
                return 1;
                break;
        }
    }

    if (optind >= argc)
    {
        print_usage();
        return 1;
    }

    filename = argv[optind];

    fd = open64(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0)
    {
        _err(errno, "During open");
        return 1;
    }

    if (trunc)
    {
        ret = ftruncate64(fd, trunc_to);
        if (ret < 0)
        {
            _err(errno, "During truncate");
            return 1;
        }
    }

    ret = pwrite64(fd, filename, strlen(filename), write_at);
    if (ret < 0)
    {
        _err(errno, "During pwrite");
        return 1;
    }

    close(fd);

    return 0;
}
