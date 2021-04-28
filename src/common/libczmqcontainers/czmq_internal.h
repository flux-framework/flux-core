/*  =========================================================================
    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of CZMQ, the high-level C binding for 0MQ:
    http://czmq.zeromq.org.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/* To avoid copying in an excess amount of code from czmq, the
 * following have been manually cut and pasted in
 */

#ifndef __CZMQ_INTERNAL__
#define __CZMQ_INTERNAL__

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
  #undef NDEBUG
  #include <assert.h>
  #define NDEBUG
#else
  #include <assert.h>
#endif

#define freen(x) do {free(x); x = NULL;} while(0)

//  Replacement for malloc() which asserts if we run out of heap, and
//  which zeroes the allocated block.
static inline void *
safe_malloc (size_t size, const char *file, unsigned line)
{
//     printf ("%s:%u %08d\n", file, line, (int) size);
    void *mem = calloc (1, size);
    if (mem == NULL) {
        fprintf (stderr, "FATAL ERROR at %s:%u\n", file, line);
        fprintf (stderr, "OUT OF MEMORY (malloc returned NULL)\n");
        fflush (stderr);
        abort ();
    }
    return mem;
}

#define zmalloc(size) safe_malloc((size), __FILE__, __LINE__)

#define streq(s1,s2) (!strcmp((s1), (s2)))

void zstr_free (char **string_p);


#endif

