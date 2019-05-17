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
#include <dlfcn.h>
#include <argz.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>

#include "module.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"

struct modfind_ctx {
    const char *modname;
    flux_moderr_f *cb;
    void *arg;
};

char *flux_modname (const char *path, flux_moderr_f *cb, void *arg)
{
    void *dso;
    const char **np;
    char *cpy = NULL;
    int saved_errno;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (!(dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL | FLUX_DEEPBIND))) {
        if (cb)
            cb (dlerror (), arg);
        errno = ENOENT;
        return NULL;
    }
    dlerror ();
    if (!(np = dlsym (dso, "mod_name"))) {
        char *errmsg;
        if (cb && (errmsg = dlerror ()))
            cb (errmsg, arg);
        errno = EINVAL;
        goto error;
    }
    if (!*np) {
        errno = EINVAL;
        goto error;
    }
    if (!(cpy = strdup (*np)))
        goto error;
    dlclose (dso);
    return cpy;
error:
    saved_errno = errno;
    dlclose (dso);
    errno = saved_errno;
    return NULL;
}

/* dirwalk_filter_f callback for dirwalk_find()
 * This function should return 1 on match, 0 on no match.
 * dirwalk_find() will stop on first match since its count parameter is 1.
 */
static int mod_find_f (dirwalk_t *d, void *arg)
{
    struct modfind_ctx *ctx = arg;
    const char *path = dirwalk_path (d);
    char *name;
    int rc = 0;

    if ((name = flux_modname (path, ctx->cb, ctx->arg))) {
        if (!strcmp (name, ctx->modname))
            rc = 1;
        free (name);
    }
    return rc;
}

char *flux_modfind (const char *searchpath,
                    const char *modname,
                    flux_moderr_f *cb,
                    void *arg)
{
    char *result = NULL;
    zlist_t *l;
    struct modfind_ctx ctx;

    if (!searchpath || !modname) {
        errno = EINVAL;
        return NULL;
    }
    ctx.modname = modname;
    ctx.cb = cb;
    ctx.arg = arg;

    l = dirwalk_find (searchpath, 0, "*.so", 1, mod_find_f, &ctx);
    if (l) {
        result = zlist_pop (l);
        zlist_destroy (&l);
    }
    if (!result)
        errno = ENOENT;
    return result;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
