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

static const char *errdecode (int errnum)
{
    if (errnum == ERANGE)
        return "set universe size would exceed maximum";
    return strerror (errnum);
}

/* strtoul() with result parameter, assumed base=10.
 * Fail if no digits, leading non-digits, leading zero,
 * or greater than IDSET_MAX_UNIVERSE.
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
    if (n >= IDSET_MAX_UNIVERSE)
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
 * Elements may appear in any order and may overlap (RFC 22).
 * On the first call set *maxid = IDSET_INVALID_ID, *count = 0, and
 * *ascending = true.  Each call builds the max id value and member count
 * in those values, and clears *ascending if this element does not ascend
 * from the previous ones, in which case *count may include duplicates.
 * On success return 0.  On failure, return -1 with errno and error set.
 */
static int append_element (struct idset *idset,
                           const char *s,
                           size_t *count,
                           unsigned int *maxid,
                           bool *ascending,
                           idset_error_t *error)
{
    unsigned int hi, lo;

    if (parse_range (s, &hi, &lo) < 0) {
        errprintf (error, "error parsing range '%s'", s);
        goto inval;
    }
    if (idset && idset_range_set (idset, lo, hi) < 0) {
        errprintf (error, "error appending '%s': %s", s, errdecode (errno));
        goto error;
    }
    *count += hi - lo + 1;
    if (*maxid != IDSET_INVALID_ID && lo <= *maxid)
        *ascending = false;
    if (*maxid == IDSET_INVALID_ID || hi > *maxid)
        *maxid = hi;
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

/* like append_element() except remove one element from 'idset' and
 * don't maintain the count or max id.
 */
static int remove_element (struct idset *idset,
                           const char *s,
                           idset_error_t *error)
{
    unsigned int hi, lo;

    if (parse_range (s, &hi, &lo) < 0) {
        errprintf (error, "error parsing range '%s'", s);
        goto inval;
    }
    if (idset && idset_range_clear (idset, lo, hi) < 0) {
        errprintf (error, "error clearing '%s': %s", s, errdecode (errno));
        goto error;
    }
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

/* Split the next comma-delimited element off the front of '*s' and return
 * it, advancing '*s' past the comma.  Set '*s' to NULL after the last
 * element.  Unlike strtok_r(), do not skip empty elements: RFC 22 does not
 * allow them, so fail with errno and error set.  An empty string has no
 * elements, so the caller should not call this function on one.
 */
static char *next_element (char **s, idset_error_t *error)
{
    char *tok = *s;
    char *comma;

    if ((comma = strchr (tok, ','))) {
        *comma = '\0';
        *s = comma + 1;
    }
    else
        *s = NULL;
    if (*tok == '\0') {
        errprintf (error, "empty element");
        errno = EINVAL;
        return NULL;
    }
    return tok;
}

static char *dup_input (const char *str, ssize_t len, idset_error_t *error)
{
    char *cpy;
    if (!str) {
        errprintf (error, "input is NULL");
        errno = EINVAL;
        return NULL;
    }
    if (len < 0)
        len = strlen (str);
    if (!(cpy = strndup (str, len))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    return cpy;
}

struct idset_range {
    unsigned int lo;
    unsigned int hi;
};

static int range_cmp (const void *a, const void *b)
{
    const struct idset_range *ra = a;
    const struct idset_range *rb = b;

    if (ra->lo < rb->lo)
        return -1;
    if (ra->lo > rb->lo)
        return 1;
    return 0;
}

/* Count the members of 'str' (up to 'len') exactly when its elements may
 * overlap, without creating an idset.  Parse the elements into an array
 * of ranges, sort by lower bound, then walk the array merging each range
 * into the current run of consecutive ids and totaling the runs.  Time and
 * memory scale with the number of elements, not the id space.  The input
 * is assumed to have already passed a parsing pass.
 * On success return 0.  On failure, return -1 with errno and error set.
 */
static int count_unordered (const char *str,
                            ssize_t len,
                            size_t *countp,
                            idset_error_t *error)
{
    char *cpy;
    char *tok, *a1;
    struct idset_range *ranges = NULL;
    size_t nranges = 0;
    size_t count = 0;
    int saved_errno;

    if (!(cpy = dup_input (str, len, error)))
        return -1;
    if (!(a1 = trim_brackets (cpy, error)))
        goto error;
    for (const char *p = a1; *p != '\0'; p++)
        if (*p == ',')
            nranges++;
    nranges++;
    if (!(ranges = calloc (nranges, sizeof (ranges[0])))) {
        errprintf (error, "out of memory");
        goto error;
    }
    nranges = 0;
    while (a1) {
        if (!(tok = next_element (&a1, error)))
            goto error;
        if (parse_range (tok, &ranges[nranges].hi, &ranges[nranges].lo) < 0) {
            errprintf (error, "error parsing range '%s'", tok);
            errno = EINVAL;
            goto error;
        }
        nranges++;
    }
    qsort (ranges, nranges, sizeof (ranges[0]), range_cmp);
    if (nranges > 0) {
        unsigned int lo = ranges[0].lo;
        unsigned int hi = ranges[0].hi;

        for (size_t i = 1; i < nranges; i++) {
            if (ranges[i].lo <= hi + 1) { // overlaps or abuts current run
                if (ranges[i].hi > hi)
                    hi = ranges[i].hi;
            }
            else {
                count += hi - lo + 1;
                lo = ranges[i].lo;
                hi = ranges[i].hi;
            }
        }
        count += hi - lo + 1;
    }
    free (ranges);
    free (cpy);
    *countp = count;
    return 0;
error:
    saved_errno = errno;
    free (ranges);
    free (cpy);
    errno = saved_errno;
    return -1;
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
    char *tok, *a1;
    int saved_errno;
    unsigned int maxid = IDSET_INVALID_ID;
    size_t count = 0;
    bool ascending = true;

    if (!(cpy = dup_input (str, len, error)))
        return -1;
    if (!(a1 = trim_brackets (cpy, error)))
        goto error;
    if (*a1 == '\0')
        a1 = NULL; // no elements: the empty set
    while (a1) {
        if (!(tok = next_element (&a1, error))
            || append_element (idset,
                               tok,
                               &count,
                               &maxid,
                               &ascending,
                               error) < 0)
            goto error;
    }
    free (cpy);
    if (maxid != IDSET_INVALID_ID && maxid >= IDSET_MAX_UNIVERSE) {
        errno = ERANGE;
        errprintf (error, "%s", errdecode (errno));
        return -1;
    }
    /* If elements did not ascend, they may overlap and 'count' may include
     * duplicates.  Re-parse to get an exact count.
     */
    if (countp && !ascending && count_unordered (str, len, &count, error) < 0)
        return -1;
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
        errprintf (error, "error creating idset object: %s", errdecode (errno));
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
    unsigned int maxid;
    if (decode_and_set_with_info (NULL, str, len, NULL, &maxid, NULL) < 0
        || maxid != IDSET_INVALID_ID)
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

int idset_decode_add (struct idset *idset,
                      const char *str,
                      ssize_t len,
                      idset_error_t *error)
{
    return decode_and_set_with_info (idset, str, len, NULL, NULL, error);
}

int idset_decode_subtract (struct idset *idset,
                           const char *str,
                           ssize_t len,
                           idset_error_t *error)
{
    char *cpy;
    char *tok, *a1;
    int saved_errno;

    if (!(cpy = dup_input (str, len, error)))
        return -1;
    if (!(a1 = trim_brackets (cpy, error)))
        goto error;
    if (*a1 == '\0')
        a1 = NULL; // no elements: the empty set
    while (a1) {
        if (!(tok = next_element (&a1, error))
            || remove_element (idset, tok, error) < 0)
            goto error;
    }
    free (cpy);
    return 0;
error:
    saved_errno = errno;
    free (cpy);
    errno = saved_errno;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
