/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "kvs.h"

char *kvs_util_normalize_key (const char *key, bool *want_directory)
{
    const char sep = '.';
    char *cpy = strdup (key);
    int i, len = strlen (key) + 1;
    bool has_sep_suffix = false;

    if (cpy) {
        /* Transform duplicate path separators into a single one.
         */
        for (i = 0; i < len; ) {
            if (cpy[i] == sep && cpy[i + 1] == sep) {
                memmove (&cpy[i], &cpy[i + 1], len - i - 1);
                len--;
            }
            else
                i++;
        }
        /* Eliminate leading path separator
         */
        if (len > 2 && cpy[0] == sep) {
            memmove (&cpy[0], &cpy[1], len - 1);
            len--;
        }
        /* Eliminate trailing path separator
         */
        if (len > 2 && cpy[len - 2] == sep) {
            cpy[len - 2] = '\0';
            len--;
            has_sep_suffix = true;
        }
        if (cpy[0] == sep)
            has_sep_suffix = true;
        if (want_directory)
            *want_directory = has_sep_suffix;
    }
    return cpy;
}

const char *kvs_get_namespace (void)
{
    const char *ns;

    if ((ns = getenv ("FLUX_KVS_NAMESPACE")))
        return ns;

    return KVS_PRIMARY_NAMESPACE;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
