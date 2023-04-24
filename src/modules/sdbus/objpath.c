/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* objpath.c - helpers for dealing with D-Bus object paths
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fnmatch.h>
#include <stdio.h>
#include <systemd/sd-bus.h>

#include "src/common/libutil/errno_safe.h"

#include "objpath.h"

static bool path_is_tooshort (const char *s)
{
    return fnmatch ("/*", s, FNM_PATHNAME) == 0 ? true : false;
}

static char *objpath_split (const char *s, const char **suffix)
{
    char *cpy;
    char *cp;

    if (!(cpy = strdup (s)))
        return NULL;
    if ((cp = strrchr (cpy, '/')))
        *cp++ = '\0';
    if (suffix)
        *suffix = cp;
    return cpy;
}

char *objpath_decode (const char *s)
{
    char *prefix;
    char *tmp = NULL;
    char *res = NULL;
    int e;

    if (path_is_tooshort (s))
        return strdup (s);
    if (!(prefix = objpath_split (s, NULL)))
        return NULL;
    e = sd_bus_path_decode (s, prefix, &tmp);
    if (e <= 0)
        goto out;
    if (asprintf (&res, "%s/%s", prefix, tmp) < 0)
        goto out;
out:
    free (tmp);
    free (prefix);
    return res;
}

char *objpath_encode (const char *s)
{
    char *prefix;
    const char *suffix;
    char *res = NULL;
    int e;

    if (path_is_tooshort (s))
        return strdup (s);
    if (!(prefix = objpath_split (s, &suffix)))
        return NULL;
    if ((e = sd_bus_path_encode (prefix, suffix, &res)) < 0) {
        errno = -e;
        goto out;
    }
out:
    free (prefix);
    return res;
}

// vi:ts=4 sw=4 expandtab
