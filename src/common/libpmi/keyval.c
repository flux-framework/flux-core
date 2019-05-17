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
#    include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
    unsigned int i;
    if (!cp)
        return EKV_NOKEY;
    i = strtoul (cp, &endptr, 10);
    if (*endptr && !isspace (*endptr))
        return EKV_VAL_PARSE;
    *val = i;
    return EKV_SUCCESS;
}

int keyval_parse_int (const char *s, const char *key, int *val)
{
    const char *cp = parse_val (s, key);
    char *endptr;
    int i;
    if (!cp)
        return EKV_NOKEY;
    i = strtol (cp, &endptr, 10);
    if (*endptr && !isspace (*endptr))
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
    if (!cp)
        return EKV_NOKEY;
    while (len > 0 && *cp && *cp != '\n') {
        *val++ = *cp++;
        len--;
    }
    if (len == 0)
        return EKV_VAL_LEN;
    *val++ = '\0';
    return EKV_SUCCESS;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
