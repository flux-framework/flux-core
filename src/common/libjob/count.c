/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <flux/idset.h>

#include "count.h"

void count_destroy (struct count *count)
{
    if (count) {
        free (count->idset);
        free (count);
    }
}

/* strtoui() with result parameter, assumed base=10.
 * Fail if no digits, leading non-digits, negative, or leading zero.
 */
static int strtoui_check (const char *s, char **endptr, unsigned int *result)
{
    long long in;

    errno = 0;
    in = strtoll (s, endptr, 10);
    // error, leading zero, non-digit, or negative input
    if (errno != 0 || *s == '0' || !isdigit (*s) || in < 1) {
        errno = errno ? errno : EINVAL;
        return -1;
    }
    *result = in;
    return 0;
}

struct count *count_decode (const char *s)
{
    struct count *count = NULL;
    char *cpy = NULL;
    char *ep;
    int len = strlen (s);

    if (!(count = calloc (1, sizeof (*count)))
        || !(cpy = strdup (s))) {
        goto error;
    }
    count->idset = NULL;
    ep = cpy;
    if (len >= 2 && s[0] == '[' && s[len - 1] == ']') {
        cpy[len - 1] = '\0';
        ++ep;
    }
    if (strchr (ep, '[') || strchr (ep, ']')) {
        // mismatched or nested brackets
        goto error_inval;
    }
    if (strtoui_check (ep, &ep, &count->min) < 0) {
        goto error;
    } else if (*ep == '\0') {
        count->integer = count->min;
        count->min = 0;
        goto success;
    }
    // assume it wasn't just a simple integer and carry on
    if ((count->idset = idset_decode (s))
         && !idset_empty (count->idset)
         && idset_first (count->idset) != 0) {
        count->min = 0;
        goto success;
    }
    // decoding as an idset failed or was invalid, so try as an RFC14 range
    free (count->idset);
    count->isrange = true;
    count->operator = '+';
    count->operand = 1;
    if (*ep == '-') {
        if (strtoui_check (ep+1, &ep, &count->max) < 0) {
            goto error;
        } else if (count->max < count->min) {
            goto error_inval;
        }
    } else if (*ep == '+') {
        count->max = COUNT_MAX;
        ++ep;
    } else {
        goto error_inval;
    }
    if (*ep == '\0') {
        goto success;
    } else if (*ep != ':') {
        goto error_inval;
    }
    if (strtoui_check (ep+1, &ep, &count->operand) < 0) {
        goto error;
    }
    if (*ep == '\0') {
        goto success;
    } else if (*ep != ':') {
        goto error_inval;
    }
    switch (ep[1]) {
        case '*':
        case '^':
            if (count->operand < 2) {
                goto error_inval;
            }
        case '+':
            count->operator = ep[1];
            break;
        default:
            goto error_inval;
    }
    if (ep[2] != '\0') {
        // input continues after operator
        goto error_inval;
    }
success:
    free (cpy);
    return count;
error_inval:
    errno = EINVAL;
error:
    free (count);
    free (cpy);
    return NULL;
}

char *count_encode (const struct count *count, int flags)
{
    char *str;
    int sz;
    int i = 0;
    int valid_flags = COUNT_FLAG_BRACKETS | COUNT_FLAG_SHORT;

    if (!count || ((flags & valid_flags) != flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (count->idset) {
        return idset_encode (count->idset, flags);
    } else if (count->integer) {
        if (asprintf (&str, "%u", count->integer) < 0) {
            return NULL;
        }
        return str;
    }
    // otherwise encode as RFC14 range
    sz = snprintf(NULL, 0, "[%u-%u:%u:+]", COUNT_MAX, COUNT_MAX, COUNT_MAX);
    if (!(str = calloc (sz+1, sizeof (*str)))) {
        return NULL;
    }
    if (count->min == count->max) {
        sprintf (str, "%u", count->min);
        return str;
    }
    if (flags & COUNT_FLAG_BRACKETS) {
        i += sprintf (str, "[");
    }
    if (count->max == COUNT_MAX) {
        i += sprintf (str+i, "%u+", count->min);
    } else {
        i += sprintf (str+i, "%u-%u", count->min, count->max);
    }
    if (!(flags & COUNT_FLAG_SHORT) || (count->operand > 1)) {
        i += sprintf (str+i, ":%u", count->operand);
    }
    if (!(flags & COUNT_FLAG_SHORT) || (count->operator != '+')) {
        i += sprintf (str+i, ":%c", count->operator);
    }
    if (flags & COUNT_FLAG_BRACKETS) {
        sprintf (str+i, "]");
    }
    return str;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
