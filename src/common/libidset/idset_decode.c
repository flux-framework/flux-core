/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <ctype.h>

#include "idset.h"
#include "idset_private.h"

static int verrprintf (idset_error_t *errp, const char *fmt, va_list ap)
{
    if (errp) {
        int saved_errno = errno;
        memset (errp->text, 0, sizeof (errp->text));
        if (fmt) {
            int n;
            n = vsnprintf (errp->text, sizeof (errp->text), fmt, ap);
            if (n > sizeof (errp->text))
                errp->text[sizeof (errp->text) - 2] = '+';
        }
        errno = saved_errno;
    }
    return -1;
}

static int errprintf (idset_error_t *errp, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    verrprintf (errp, fmt, ap);
    va_end (ap);
    return -1;
}

/* strtoul() with result parameter, assumed base=10.
 * Fail if no digits, leading non-digits, or leading zero.
 * Returns 0 on success, -1 on failure.
 */
static int strtoul_check (const char *s, char **endptr, unsigned long *result)
{
    unsigned long n;
    char *ep;

    errno = 0;
    n = strtoul (s, &ep, 10);
    if (errno != 0)
        return -1;
    if (ep == s) // no digits
        return -1;
    if (!isdigit (*s))
        return -1;
    if (*s == '0' && ep - s > 1) // leading zero (RFC 22)
        return -1;
    *result = n;
    if (endptr)
        *endptr = ep;
    return 0;
}

static int parse_range (const char *s, unsigned int *hi, unsigned int *lo)
{
    char *endptr;
    unsigned int h, l;
    unsigned long n;

    if (strtoul_check (s, &endptr, &n) < 0)
        return -1;
    if (*endptr != '\0' && *endptr != '-')
        return -1;
    h = l = n;
    if (*endptr == '-') {
        s = endptr + 1;
        if (strtoul_check (s, &endptr, &n) < 0)
            return -1;
        if (*endptr != '\0')
            return -1;
        if (n <= l)
            return -1;
        h = n;
    }
    *hi = h;
    *lo = l;
    return 0;
}

/* Append one element (single digit or range) to 'idset'.
 * Each element must ascend from the previous ones.
 * On the first call set *maxid = IDSET_INVALID_ID and *count = 0.
 * Each call builds the max id value and member count in those values.
 * On success return 0.  On failure, return -1 with errno and error set.
 */
static int append_element (struct idset *idset,
                           const char *s,
                           size_t *count,
                           unsigned int *maxid,
                           idset_error_t *error)
{
    unsigned int hi, lo;

    if (parse_range (s, &hi, &lo) < 0) {
        errprintf (error, "error parsing range '%s'", s);
        goto inval;
    }
    if (*maxid != IDSET_INVALID_ID && lo <= *maxid) {
        errprintf (error, "range '%s' is out of order", s);
        goto inval;
    }
    if (idset && idset_range_set (idset, lo, hi) < 0) {
        errprintf (error, "error appending '%s': %s", s, strerror (errno));
        goto error;
    }
    *count += hi - lo + 1;
    *maxid = hi;
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

/* Trim brackets by dropping a \0 on the tail of 's' and returning a starting
 * pointer within 's'.  On failure return NULL with errno and error set.
 */
static char *trim_brackets (char *s, idset_error_t *error)
{
    int len = strlen (s);

    if (len >= 2 && s[0] == '[' && s[len - 1] == ']') {
        s[len - 1] = '\0';
        s++;
    }
    if (strchr (s, '[') || strchr (s, ']')) {
        errprintf (error, "mismatched or nested brackets");
        errno = EINVAL;
        return NULL;
    }
    return s;
}

/* Decode 'str' (up to 'len') and add it to 'idset'.
 * If 'idset' is NULL, this is a parsing pass to determine count/maxid only.
 * If 'len' < 0, use strlen (str).
 * If non-NULL, return member count in *countp, max id in *maxidp.
 * On success return 0.  On failure, return -1 with errno and error set.
 */
static int decode_and_set_with_info (struct idset *idset,
                                     const char *str,
                                     ssize_t len,
                                     size_t *countp,
                                     unsigned int *maxidp,
                                     idset_error_t *error)
{
    char *cpy;
    char *tok, *saveptr, *a1;
    int saved_errno;
    unsigned int maxid = IDSET_INVALID_ID;
    size_t count = 0;

    if (!str) {
        errprintf (error, "input is NULL");
        errno = EINVAL;
        return -1;
    }
    if (len < 0)
        len = strlen (str);
    if (!(cpy = strndup (str, len))) {
        errprintf (error, "out of memory");
        return -1;
    }
    if (!(a1 = trim_brackets (cpy, error)))
        goto error;
    saveptr = NULL;
    while ((tok = strtok_r (a1, ",", &saveptr))) {
        if (append_element (idset, tok, &count, &maxid, error) < 0)
            goto error;
        a1 = NULL;
    }
    free (cpy);
    if (countp)
        *countp = count;
    if (maxidp)
        *maxidp = maxid;
    return 0;
error:
    saved_errno = errno;
    free (cpy);
    errno = saved_errno;
    return -1;
}

struct idset *idset_decode_ex (const char *str,
                               ssize_t len,
                               ssize_t size,
                               int flags,
                               idset_error_t *error)
{
    struct idset *idset;

    if (size < 0) {
        unsigned int maxid;
        if (decode_and_set_with_info (NULL, str, len, NULL, &maxid, error) < 0)
            return NULL;
        if (maxid != IDSET_INVALID_ID)
            size = maxid + 1;
        else if ((flags & IDSET_FLAG_AUTOGROW))
            size = 1;
        else {
            errprintf (error, "cannot create an empty idset");
            errno = EINVAL;
            return NULL;
        }
    }
    if (!(idset = idset_create (size, flags))) {
        errprintf (error, "error creating idset object: %s", strerror (errno));
        return NULL;
    }
    if (decode_and_set_with_info (idset, str, len, NULL, NULL, error) < 0) {
        idset_destroy (idset);
        return NULL;
    }
    return idset;
}

struct idset *idset_ndecode (const char *str, size_t len)
{
    return idset_decode_ex (str, len, 0, IDSET_FLAG_AUTOGROW, NULL);
}

struct idset *idset_decode (const char *str)
{
    return idset_decode_ex (str, -1, 0, IDSET_FLAG_AUTOGROW, NULL);
}

bool idset_decode_empty (const char *str, ssize_t len)
{
    size_t count;
    if (decode_and_set_with_info (NULL, str, len, &count, NULL, NULL) < 0
        || count > 0)
        return false;
    return true;
}

int idset_decode_info (const char *str,
                       ssize_t len,
                       size_t *count,
                       unsigned int *maxid,
                       idset_error_t *error)
{
    return decode_and_set_with_info (NULL, str, len, count, maxid, error);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
