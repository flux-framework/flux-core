/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <czmq.h>

#include "src/common/libflux/handle.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/message.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "reactor.h"

struct msg_compat {
    FluxMsgHandler fn;
    void *arg;
    flux_msg_watcher_t *w;
    int typemask;
    char *pattern;
};

struct ctx {
    zlist_t *msg_watchers;
};

static void msg_compat_destroy (struct msg_compat *c)
{
    if (c) {
        if (c->pattern)
            free (c->pattern);
        free (c);
    }
}

static void freectx (void *arg)
{
    struct ctx *ctx = arg;

    if (ctx->msg_watchers) {
        struct msg_compat *c;
        while ((c = zlist_pop (ctx->msg_watchers)))
            msg_compat_destroy (c);
        zlist_destroy (&ctx->msg_watchers);
    }
    free (ctx);
}

static struct ctx *getctx (flux_t h)
{
    struct ctx *ctx = flux_aux_get (h, "reactor_compat");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->msg_watchers = zlist_new ()))
            oom ();
        flux_aux_set (h, "reactor_compat", ctx, freectx);
    }
    return ctx;
}


void msg_compat_cb (flux_t h, struct flux_msg_watcher *w,
                    const flux_msg_t *msg, void *arg)
{
    struct msg_compat *compat = arg;
    flux_msg_t *cpy = NULL;
    int type;

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (compat->fn (h, type, &cpy, compat->arg) != 0)
        flux_reactor_stop_error (h);
done:
    flux_msg_destroy (cpy);
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    struct ctx *ctx = getctx (h);
    struct msg_compat *c = xzmalloc (sizeof (*c));
    struct flux_match match = {
        .typemask = typemask,
        .topic_glob = (char *)pattern,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 1,
    };

    c->fn = cb;
    c->arg = arg;
    c->typemask = typemask;
    c->pattern = xstrdup (pattern);
    if (flux_msg_watcher_add (h, match, msg_compat_cb, c, &c->w) < 0) {
        msg_compat_destroy (c);
        return -1;
    }
    if (zlist_append (ctx->msg_watchers, c) < 0)
        oom ();
    return 0;
}

int flux_msghandler_addvec (flux_t h, msghandler_t *hv, int len, void *arg)
{
    int i;

    for (i = 0; i < len; i++)
        if (flux_msghandler_add (h, hv[i].typemask, hv[i].pattern,
                                    hv[i].cb, arg) < 0)
            return -1;
    return 0;
}

static bool matchstr (const char *s1, const char *s2)
{
    if ((s1 == NULL && s2 == NULL) || (s1 && s2 && !strcmp (s1, s2)))
        return true;
    return false;
}

void flux_msghandler_remove (flux_t h, int typemask, const char *pattern)
{
    struct ctx *ctx = getctx (h);
    struct msg_compat *c;

    c = zlist_first (ctx->msg_watchers);
    while (c) {
        if (c->typemask == typemask && matchstr (c->pattern, pattern)) {
            flux_msg_watcher_cancel (c->w);
            zlist_remove (ctx->msg_watchers, c);
            msg_compat_destroy (c);
            break;
        }
        c = zlist_next (ctx->msg_watchers);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
