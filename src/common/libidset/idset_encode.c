/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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

#include "idset.h"
#include "idset_private.h"

/* Format a string like printf, then append it to *s.
 * The allocated size of '*s' is '*sz'.
 * The current string length of '*s' is '*len'.
 * Grow *s by IDSET_ENCODE_CHUNK to allow new string to be appended.
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
        size_t nsz = *sz + IDSET_ENCODE_CHUNK;
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

    if (validate_idset_flags (flags, IDSET_FLAG_BRACKETS
                                   | IDSET_FLAG_RANGE) < 0)
        return NULL;
    if (!idset) {
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
