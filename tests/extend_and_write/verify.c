#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define PRINTERR(err)                                                         \
        printf("[%d] Error %d (Line %d, Function \"%s\"): \"%s\"\n",	      \
               getpid(), err, __LINE__, __FUNCTION__, strerror(err))

#define SZ 1024
#define NUMWRITES 10240

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

int main(int argc, char **argv)
{
	int fd;
	char buffer[SZ];
	int i;
	int numas = 0;
	int numbs = 0;
	int bytes;
	int failed = 0;

	printf("file should be %d bytes\n", 2 * SZ * NUMWRITES);
	fd = open("testfile", O_RDONLY);
	if (fd == -1) {
		failed = 1;
		PRINTERR(errno);
		goto bail;
	}

	/* there should be exactly NUMWRITES a's and NUMBWRITES b's */
	for(i = 0; i < (2*NUMWRITES); i++) {
		bytes = read(fd, buffer, SZ);
		if (bytes < 0) {		
			failed = 1;
			PRINTERR(errno);
			goto bail;
		}
		if (bytes != SZ) {
			printf("file is to small, could only read %d bytes\n",
			       (i * SZ + bytes));
			failed = 1;
			break;
		}
		if (buffer[0] != 'A' && buffer[0] != 'B') {
			printf("invalid data found, char %c encountered\n", 
			       buffer[0]);
			failed = 1;
			break;
		}

		failed = checkbuff(buffer, SZ, buffer[0]);
		if (failed) {
			printf("invalid data found in block\n");
			break;
		}
		if (buffer[0] == 'A')
			numas++;
		else
			numbs++;
	}

	if (!failed)
		if ((numas != numbs)    || 
		    (numas < NUMWRITES) || 
		    (numbs < NUMWRITES)) {
			printf("should have %d A's and %d B's but have %d A's "
			       "and %d B's\n", NUMWRITES, NUMWRITES, numas, 
			       numbs);
			failed = 1;
		}
	if (failed)
		printf("file failed verification\n");
bail:
	return(failed);
}
