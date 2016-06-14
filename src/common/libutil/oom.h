#ifndef _UTIL_OOM_H
#define _UTIL_OOM_H

#include <stdio.h>

#define oom() do { \
    fprintf (stderr, "%s::%s(), line %d: Out of memory\n", \
             __FILE__, __FUNCTION__, __LINE__); \
    exit (1); \
} while (0)

#endif /* !_UTIL_OOM_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
