#ifndef API_REFLINK_H
#define API_REFLINK_H

#ifdef NO_REFLINK

int reflink(const char *oldpath, const char *newpath, unsigned long preserve);

#endif

#endif

