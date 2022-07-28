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
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/libyuarel/yuarel.h"
#include "popen2.h"
#include "read_all.h"
#include "log.h"

static void nullify_newline (char *str)
{
    int n;
    if (str && (n = strlen (str)) > 0) {
        if (str[n-1] == '\n')
            str[n-1] = '\0';
    }
}

static char *uri_to_local (struct yuarel *yuri)
{
    char *uri = NULL;
    if (asprintf (&uri, "local:///%s", yuri->path) < 0)
        return NULL;
    return uri;
}

char *uri_resolve (const char *uri)
{
    struct popen2_child *child = NULL;
    struct yuarel yuri;
    char *result = NULL;
    char *argv[] = { "flux", "uri", (char *) uri, NULL };

    char *cpy = strdup (uri);
    if (!cpy)
        return NULL;
    if (yuarel_parse (&yuri, cpy) == 0 && yuri.scheme) {
        if (strcmp (yuri.scheme, "ssh") == 0
            || strcmp (yuri.scheme, "local") == 0) {
            if (getenv ("FLUX_URI_RESOLVE_LOCAL"))
                result = uri_to_local (&yuri);
            else
                result = strdup (uri);
            free (cpy);
            return result;
        }
    }
    free (cpy);

    if (!(child = popen2 ("flux", argv, 0))
        || (read_all (popen2_get_fd (child), (void **)&result) < 0))
        goto out;
    nullify_newline (result);
out:
    if (pclose2 (child) != 0) {
        /* flux-uri returned error */
        free (result);
        result = NULL;
    }
    return result;
}


// vi:ts=4 sw=4 expandtab
