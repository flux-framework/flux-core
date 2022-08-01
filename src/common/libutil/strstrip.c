/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <errno.h>
#include <ctype.h>

char *strstrip (char *s)
{
    size_t size;
    char *end;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    size = strlen (s);

    if (!size)
        return s;

    end = s + size - 1;
    while (end >= s && isspace ((unsigned char) *end))
        end--;
    *(end + 1) = '\0';

    while (*s && isspace((unsigned char) *s))
        s++;

    return s;
}

char *strstrip_copy (const char *s)
{
    size_t size;
    char *end;
    char *result = NULL;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    if (s[0] == '\0')
        return strdup (s);

    /* Work from front to back so we do not have to copy all of 's'
     */
    while (*s && isspace ((unsigned char) *s))
        s++;

    if (!(result = strdup (s)))
        return NULL;

    if ((size = strlen (result)) == 0)
        return result;

    end = result + size - 1;
    while (end >= result && isspace ((unsigned char) *end))
        end--;
    *(end + 1) = '\0';

    return result;
}

/* vi: ts=4 sw=4 expandtab
 */
