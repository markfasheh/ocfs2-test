#ifndef API_SPLICE_H
#define API_SPLICE_H

#include <unistd.h>
#include <fcntl.h>

#if defined(__i386__)

#define __NR_splice	313

#elif defined(__x86_64__)

#define __NR_splice	275

#elif defined(__powerpc__)

#define __NR_splice	283

#elif defined(__s390__)

#define __NR_splice	306

#elif defined(__ia64__)

#define __NR_splice	1297

#else
#error unsupported arch
#endif

int splice(int fdin, loff_t *off_in, int fdout,
			 loff_t *off_out, size_t len, unsigned int flags)
{
	return syscall(__NR_splice, fdin, off_in, fdout, off_out, len, flags);
}

#endif
