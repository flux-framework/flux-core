/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* idset - a set of numerically sorted, non-negative integers */

/* Implemented as a wrapper around a Van Emde Boas tree.
 * T.D is data; T.M is size
 * All ops are O(log m), for key bitsize m: 2^m == T.M.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "src/common/libutil/veb.h"

#include "idset.h"

#define IDSET_MAGIC 0xf00f0ee1
struct idset {
    int magic;
    Veb T;
    int flags;
};

#define ENCODE_CHUNK 1024

struct idset *idset_create (size_t size, int flags)
{
    struct idset *idset;

    if (size == 0 || (flags & ~IDSET_FLAG_AUTOGROW) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(idset = malloc (sizeof (*idset))))
        return NULL;
    idset->magic = IDSET_MAGIC;
    idset->T = vebnew (size, 0);
    if (!idset->T.D) {
        free (idset);
        errno = ENOMEM;
        return NULL;
    }
    idset->flags = flags;
    return idset;
}

void idset_destroy (struct idset *idset)
{
    if (idset) {
        idset->magic = ~IDSET_MAGIC;
        free (idset->T.D);
        free (idset);
    }
}

/* Format a string like printf, then append it to *s.
 * The allocated size of '*s' is '*sz'.
 * The current string length of '*s' is '*len'.
 * Grow *s by ENCODE_CHUNK to allow new string to be appended.
 * Returns 0 on success, -1 on failure with errno = ENOMEM.
 */
static int __attribute__ ((format (printf, 4, 5)))
catprintf (char **s, size_t *sz, size_t *len, const char *fmt, ...)
{
    va_list ap;
    char *ns;
    size_t nlen;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&ns, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return -1;
    nlen = strlen (ns);

    while (*len + nlen + 1 > *sz) {
        size_t nsz = *sz + ENCODE_CHUNK;
        char *p;
        if (!(p = realloc (*s, nsz)))
            goto error;
        if (*s == NULL)
            *p = '\0';
        *s = p;
        *sz = nsz;
    }
    strcat (*s, ns);
    *len += nlen;
    free (ns);
    return 0;
error:
    free (ns);
    errno = ENOMEM;
    return -1;
}

static int catrange (char **s, size_t *sz, size_t *len,
                     unsigned int lo, unsigned int hi, const char *sep)
{
    int rc;
    if (lo == hi)
        rc = catprintf (s, sz, len, "%u%s", lo, sep);
    else
        rc = catprintf (s, sz, len, "%u-%u%s", lo, hi, sep);
    return rc;
}

/* Return value: count of id's in set, or -1 on failure.
 * N.B. if count is more than INT_MAX, return value is INT_MAX.
 */
static int encode_ranged (const struct idset *idset,
                          char **s, size_t *sz, size_t *len)
{
    int count = 0;
    unsigned int id;
    unsigned int lo = 0;
    unsigned int hi = 0;
    bool first = true;

    lo = hi = id = vebsucc (idset->T, 0);
    while (id < idset->T.M) {
        unsigned int next = vebsucc (idset->T, id + 1);;
        bool last = (next == idset->T.M);

        if (first)                  // first iteration
            first = false;
        else if (id == hi + 1)      // id is in range
            hi++;
        else {                      // id is NOT in range
            if (catrange (s, sz, len, lo, hi, ",") < 0)
                return -1;
            lo = hi = id;
        }
        if (last) {                 // last iteration
            if (catrange (s, sz, len, lo, hi, last ? "" : ",") < 0)
                return -1;
        }
        if (count < INT_MAX)
            count++;
        id = next;
    }
    return count;
}

/* Return value: count of id's in set, or -1 on failure.
 * N.B. if count is more than INT_MAX, return value is INT_MAX.
 */
static int encode_simple (const struct idset *idset,
                          char **s, size_t *sz, size_t *len)
{
    int count = 0;
    unsigned int id;

    id = vebsucc (idset->T, 0);
    while (id != idset->T.M) {
        int next = vebsucc (idset->T, id + 1);
        char *sep = next == idset->T.M ? "" : ",";
        if (catprintf (s, sz, len, "%d%s", id, sep) < 0)
            return -1;
        if (count < INT_MAX)
            count++;
        id = next;
    }
    return count;
}

char *idset_encode (const struct idset *idset, int flags)
{
    char *str = NULL;
    size_t strsz = 0;
    size_t strlength = 0;
    int count;

    if (!idset || idset->magic != IDSET_MAGIC
               || (flags & ~(IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & IDSET_FLAG_BRACKETS)) {    // add open brace, if requested
        if (catprintf (&str, &strsz, &strlength, "[") < 0)
            goto error;
    }
    if ((flags & IDSET_FLAG_RANGE))
        count = encode_ranged (idset, &str, &strsz, &strlength);
    else
        count = encode_simple (idset, &str, &strsz, &strlength);
    if (count < 0)
        goto error;
    if ((flags & IDSET_FLAG_BRACKETS) && count > 1) { // add close brace
        if (catprintf (&str, &strsz, &strlength, "]") < 0)
            goto error;
    }
    if (!str) {
        if (!(str = strdup ("")))
            goto error;
    }
    if (count <= 1 && str[0] == '[')        // no braces for singletons
        memmove (str, str + 1, strlength);  // moves '\0' too
    return str;
error:
    free (str);
    errno = ENOMEM;
    return NULL;
}

/* Grow idset to next power of 2 size that has at least 'size' slots.
 * Return 0 on success, -1 on failure with errno == ENOMEM.
 */
static int idset_grow (struct idset *idset, size_t size)
{
    size_t newsize = idset->T.M;
    Veb T;
    unsigned int id;

    while (newsize <= size)
        newsize <<= 1;

    if (newsize > idset->T.M) {
        T = vebnew (newsize, 0);
        if (!T.D)
            return -1;

        id = vebsucc (idset->T, 0);
        while (id < idset->T.M) {
            vebput (T, id);
            id = vebsucc (idset->T, id + 1);
        }
        free (idset->T.D);
        idset->T = T;
    }
    return 0;
}

static int idset_set (struct idset *idset, unsigned int id)
{
    if (id == UINT_MAX || idset_grow (idset, id + 1) < 0)
        return -1;
    vebput (idset->T, id);
    return 0;
}

static int parse_range (const char *s, unsigned int *hi, unsigned int *lo)
{
    char *endptr;
    unsigned int h, l;
    unsigned long n;

    n = strtoul (s, &endptr, 10);
    if (n >= UINT_MAX || endptr == s || (*endptr != '\0' && *endptr != '-'))
        return -1;
    h = l = n;
    if (*endptr == '-') {
        s = endptr + 1;
        n = strtoul (s, &endptr, 10);
        if (n >= UINT_MAX || endptr == s || *endptr != '\0')
            return -1;
        h = n;
    }
    if (h >= l) {
        *hi = h;
        *lo = l;
    }
    else {
        *hi = l;
        *lo = h;
    }
    return 0;
}

static char *trim_brackets (char *s)
{
    char *p = s;
    if (*p == '[')
        p++;
    size_t len = strlen (p);
    if (len > 0 && p[len - 1] == ']')
        p[len - 1] = '\0';
    return p;
}

struct idset *idset_decode (const char *str)
{
    struct idset *idset;
    char *cpy = NULL;
    char *tok, *saveptr, *a1;
    int saved_errno;

    if (!str) {
        errno = EINVAL;
        return NULL;
    }
    if (!(idset = idset_create (1024, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (!(cpy = strdup (str)))
        goto error;
    a1 = trim_brackets (cpy);
    saveptr = NULL;
    while ((tok = strtok_r (a1, ",", &saveptr))) {
        unsigned int hi, lo, i;
        if (parse_range (tok, &hi, &lo) < 0)
            goto inval;
        /* Count backwards so that idset_set() can grow the
         * idset to the maximum size on the first access,
         * rather than possibly doing it multiple times.
         */
        for (i = hi; i >= lo && i != UINT_MAX; i--) {
            if (idset_set (idset, i) < 0)
                goto error;
        }
        a1 = NULL;
    }
    free (cpy);
    return idset;
inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    idset_destroy (idset);
    free (cpy);
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
