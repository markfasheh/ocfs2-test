#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8001

int
main (int argc, char **argv)
{
    int fd, sd;
    char *src, *host;
    struct stat sbuf;
    int ret;
    off_t offset = 0;
    struct sockaddr_in sin;
    struct hostent *hp;

    if (argc < 3) {
        fputs("not enough arguments\n", stderr);
        exit(1);
    }

    src = argv[1];
    host = argv[2];

    fd = open(src, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    if (fstat(fd, &sbuf) == -1) {
        perror("fstat");
        exit(1);
    }

    hp = gethostbyname(host);
    if (hp == NULL) {
        perror("gethostbyname");
        exit(1);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
    sin.sin_port = htons(PORT);

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("socket");
        exit(1);
    }

    if (connect(sd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        perror("connect");
        exit(1);
    }

    ret = sendfile(sd, fd, &offset, sbuf.st_size);
    if (ret == -1) {
        perror("sendfile");
        exit(1);
    } else if (ret != sbuf.st_size) {
        fputs("sendfile didn't send the whole thing\n", stderr);
        exit(1);
    }

    close(fd);
    close(sd);

    return 0;
}
