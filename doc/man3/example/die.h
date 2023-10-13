#ifndef _EXAMPLE_DIE
#define _EXAMPLE_DIE

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#define die(fmt, ...) do { \
    int _e = errno; \
    fprintf (stderr, (fmt), ##__VA_ARGS__); \
    fprintf (stderr, ": %s\n", strerror (_e)); \
    exit (1); \
} while (0);
#endif // _EXAMPLE_DIE
