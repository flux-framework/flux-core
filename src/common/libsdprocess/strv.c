/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <errno.h>

#include "strv.h"

void strv_destroy (char **strv)
{
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            free (*ptr);
            ptr++;
        }
        free (strv);
    }
}

static int strv_len_delim (char *str, char *delim, size_t *lenp)
{
    size_t len = 0;

    if (str) {
        char *cpy = NULL;
        char *itrcpy = NULL;
        char *ptr;
        if (!(cpy = itrcpy = strdup (str)))
            return -1;

        while ((ptr = strtok (itrcpy, " "))) {
            len++;
            itrcpy = NULL;
        }
        free (cpy);
    }
    (*lenp) = len;
    return 0;
}

char **strv_create (char *str, char *delim)
{
    char *cpy = NULL;
    char *itrcpy = NULL;
    char *ptr = NULL;
    size_t len = 0;
    int i;
    char **strv = NULL;

    if (!str || !delim) {
        errno = EINVAL;
        return NULL;
    }

    if (strv_len_delim (str, delim, &len) < 0)
        return NULL;

    /* for NULL pointer at end */
    len++;

    if (!(strv = calloc (len, sizeof (char *))))
        return NULL;

    if (!(cpy = itrcpy = strdup (str))) {
        free (strv);
        return NULL;
    }

    i = 0;
    while ((ptr = strtok (itrcpy, delim))) {
        if (!(strv[i++] = strdup (ptr))) {
            strv_destroy (strv);
            free (cpy);
            return NULL;
        }
        itrcpy = NULL;
    }

    free (cpy);
    return strv;
}

static int strv_len (char **strv)
{
    int len = 0;
    if (strv) {
        char **ptr = strv;
        while (*ptr) {
            len++;
            ptr++;
        }
    }
    return len;
}

int strv_copy (char **strv, char ***strv_cpy)
{
    char **cpy = NULL;
    int i, len;

    if (!strv || !strv_cpy) {
        errno = EINVAL;
        return -1;
    }

    len = strv_len (strv);
    /* +1 for NULL terminating pointer */
    if (!(cpy = calloc (1, sizeof (char *) * (len + 1))))
        return -1;
    for (i = 0; i < len; i++) {
        if (!(cpy[i] = strdup (strv[i]))) {
            strv_destroy (cpy);
            return -1;
        }
    }
    (*strv_cpy) = cpy;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
