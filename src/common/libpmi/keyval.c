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
# include "config.h"
#endif
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "ccan/str/str.h"
#include "keyval.h"

static const char *parse_val (const char *s, const char *key)
{
    char *match;
    const char *cp = s;
    int keylen = strlen (key);

    while ((match = strstr (cp, key))) {
        cp = match;
        if (cp[keylen] == '=' && (cp == s || isspace (*(cp - 1))))
            return cp + keylen + 1;
        cp++;
    }
    return NULL;
}

int keyval_parse_uint (const char *s, const char *key, unsigned int *val)
{
    const char *cp = parse_val (s, key);
    char *endptr;
    unsigned long i;
    if (!cp)
        return EKV_NOKEY;
    errno = 0;
    i = strtoul (cp, &endptr, 10);
    if (errno != 0
        || (*endptr && !isspace (*endptr))
        || i > UINT_MAX)
        return EKV_VAL_PARSE;
    *val = i;
    return EKV_SUCCESS;
}

int keyval_parse_int (const char *s, const char *key, int *val)
{
    const char *cp = parse_val (s, key);
    char *endptr;
    long i;
    if (!cp)
        return EKV_NOKEY;
    errno = 0;
    i = strtol (cp, &endptr, 10);
    if (errno != 0
        || (*endptr && !isspace (*endptr))
        || i > INT_MAX
        || i < INT_MIN)
        return EKV_VAL_PARSE;
    *val = i;
    return EKV_SUCCESS;
}

int keyval_parse_word (const char *s, const char *key, char *val, int len)
{
    const char *cp = parse_val (s, key);
    if (!cp)
        return EKV_NOKEY;
    while (len > 0 && *cp && !isspace (*cp)) {
        *val++ = *cp++;
        len--;
    }
    if (len == 0)
        return EKV_VAL_LEN;
    *val++ = '\0';
    return EKV_SUCCESS;
}

int keyval_parse_isword (const char *s, const char *key, const char *match)
{
    int len = strlen (match);
    const char *cp = parse_val (s, key);
    if (!cp)
        return EKV_NOKEY;
    while (len > 0 && *cp && *cp++ == *match++)
        len--;
    if (len > 0)
        return EKV_VAL_NOMATCH;
    return EKV_SUCCESS;
}

int keyval_parse_string (const char *s, const char *key, char *val, int len)
{
    const char *cp = parse_val (s, key);
    char *vp = val;
    if (!cp)
        return EKV_NOKEY;
    while (len > 0 && *cp && *cp != '\n') {
        *vp++ = *cp++;
        len--;
    }
    if (len == 0)
        return EKV_VAL_LEN;
    *vp++ = '\0';
   /* Quirk: mpiexec.hydra from mpich v4.2.0 and v4.1.1 appends "found=TRUE"
    * to KVS get responses due to a presumed bug.  Ignore.
    * See flux-framework/flux-core#6072.
    */
    if (streq (key, "value") && strends (val, " found=TRUE"))
        val[strlen (val) - 11] = '\0';
    return EKV_SUCCESS;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
