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
#include "ccan/str/str.h"
#include "errprintf.h"
#include "popen2.h"
#include "read_all.h"
#include "log.h"
#include "strstrip.h"
#include "errno_safe.h"
#include "uri.h"

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

char *uri_remote_get_authority (const char *uri)
{
    struct yuarel yuri;
    char *cpy;
    char *result = NULL;

    if (!uri)
        return NULL;

    cpy = strdup (uri);
    if (yuarel_parse (&yuri, cpy) == 0
        && yuri.scheme
        && streq (yuri.scheme, "ssh")) {
        if (asprintf (&result,
                      "%s%s%s",
                      yuri.username ? yuri.username : "",
                      yuri.username ? "@" : "",
                      yuri.host) < 0)
            result = NULL;
    }
    ERRNO_SAFE_WRAP (free, cpy);
    return result;
}

char *uri_resolve (const char *uri, flux_error_t *errp)
{
    struct popen2_child *child = NULL;
    struct yuarel yuri;
    char *result = NULL;
    char *errors = NULL;
    char *argv[] = { "flux", "uri", (char *) uri, NULL };
    int flags = errp != NULL ? POPEN2_CAPTURE_STDERR : 0;

    char *cpy = strdup (uri);
    if (!cpy)
        return NULL;
    if (yuarel_parse (&yuri, cpy) == 0 && yuri.scheme) {
        if (streq (yuri.scheme, "ssh")
            || streq (yuri.scheme, "local")) {
            if (getenv ("FLUX_URI_RESOLVE_LOCAL"))
                result = uri_to_local (&yuri);
            else
                result = strdup (uri);
            free (cpy);
            return result;
        }
    }
    free (cpy);

    if (!(child = popen2 ("flux", argv, flags))
        || (read_all (popen2_get_fd (child), (void **)&result) < 0))
        goto out;
    nullify_newline (result);

    if (errp) {
        /* stderr capture requested */
        if (read_all (popen2_get_stderr_fd (child), (void **)&errors) > 0) {
            errprintf (errp, "%s", strstrip (errors));
            free (errors);
        }
    }
out:
    if (pclose2 (child) != 0) {
        /* flux-uri returned error */
        free (result);
        result = NULL;
    }
    return result;
}


// vi:ts=4 sw=4 expandtab
