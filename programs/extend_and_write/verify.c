#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define PROGNAME "verify"
#define PRINTERR(err)                                                         \
	printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",	      \
		getpid(), err, __LINE__, __FUNCTION__, strerror(err))

#define SZ 1024
#define NUMWRITES 10240

int count;

int checkbuff(char buffer[], int size, char c);

int checkbuff(char buffer[], int size, char c)
{
	int failed = 0;
	int i;

	for(i = 0; i < size; i++)
		if (buffer[i] != c) {
			failed = 1;
			break;
		}
	return(failed);
}

static void Usage()
{
	printf("\nUsage: %s -h -f <filename> [-s <extend size>]"
		" [-n <numwrites>]\n", PROGNAME);
	printf("Will verify a file created by  extend_and_write.\n\n"
		"<extend size> Size of the extend (Default=1024)\n"
		"<numwrites> Number of writes to be performed (Default=10240).\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int fd;
	int i, c;
	char filename[256] = "/tmp/extend_and_write.txt";
	int numas = 0;
	int off_t = SZ;
	int loops = NUMWRITES;
	int numbs = 0;
	int bytes;
	int failed = 0;

	opterr = 0;
	if (argc < 5) {
		Usage();
	}
	count = argc;
	while ((c = getopt(argc, argv, ":h:f:s:n:H:-:")) != EOF) {
		switch (c) {
		case 'h':
			Usage();
			break;
		case 'H':
			Usage();
			break;
		case '-':
			if (!strcmp(optarg, "help"))
				Usage();
			else {
				fprintf(stderr, "%s: Invalid option:"
					" \'--%s\'\n",
					PROGNAME, optarg);
				return -EINVAL;
			}
		case 'f':
			snprintf(filename,255,"%s",optarg);
			if ((*filename == '-')) {
				fprintf(stderr, "%s: Invalid file name: %s\n",
					PROGNAME, optarg);
				return -EINVAL;
			}
			break;
		case 'n':
			loops = atoi(optarg);
			if (!loops) {
				fprintf(stderr, "%s: Invalid numwrites: %s\n",
					PROGNAME, optarg);
				return -EINVAL;
			}
			break;
		case 's':
			off_t = atoi(optarg);
			if (!off_t) {
				fprintf(stderr, "%s: Invalid extend size: %s\n",
					PROGNAME, optarg);
			return -EINVAL;
			}
			break;
		default:
			Usage();
			break;
		}
	}
	char buffer[off_t];

	printf("%s: Starting\n", PROGNAME);
	printf("%s: file should be %d bytes\n", PROGNAME, (2 * off_t * loops));
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		failed = 1;
		PRINTERR(errno);
		goto bail;
	}

	/* there should be exactly NUMWRITES a's and NUMBWRITES b's */
	for(i = 0; i < (2*loops); i++) {
		bytes = read(fd, buffer, off_t);
		if (bytes < 0) {
			failed = 1;
			PRINTERR(errno);
			goto bail;
		}
		if (bytes != off_t) {
			printf("%s: File is to small, could only read %d "
				"bytes\n", PROGNAME, (i * off_t + bytes));
			failed = 1;
			break;
		}
		if (buffer[0] != 'A' && buffer[0] != 'B') {
			printf("%s: Invalid data found, char %c encountered\n",
				PROGNAME, buffer[0]);
			failed = 1;
			break;
		}

		failed = checkbuff(buffer, off_t, buffer[0]);
		if (failed) {
			printf("%s: Invalid data found in block\n", PROGNAME);
			break;
		}
		if (buffer[0] == 'A')
			numas++;
		else
			numbs++;
	}

	if (!failed)
		if ((numas != numbs)    ||
			(numas < loops) ||
			(numbs < loops)) {
			printf("%s: should have %d A's and %d B's but have %d"
				"A's and %d B's\n",
				PROGNAME, loops, loops, numas, numbs);
			failed = 1;
		}
	if (failed)
		printf("%s: File verification failed.\n", PROGNAME);
	else
		printf("%s: File verification successful.\n", PROGNAME);

bail:
	return(failed);
}
