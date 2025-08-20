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
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#if HAVE_LIBUV
# include <uv.h>
#elif HAVE_LIBEV_INTERNAL
# include "src/common/libev/ev.h"
#else
# include <ev.h>
#endif
#include <flux/core.h>

#include "reactor_private.h"

struct flux_reactor {
#if HAVE_LIBUV
    uv_loop_t loop;
#else
    struct ev_loop *loop;
#endif
    int usecount;
    unsigned int errflag:1;
};

void flux_reactor_decref (flux_reactor_t *r)
{
    if (r && --r->usecount == 0) {
        int saved_errno = errno;
#if HAVE_LIBUV
        (void)uv_loop_close (&r->loop); // could return -EBUSY
#else
        ev_loop_destroy (r->loop);
#endif
        free (r);
        errno = saved_errno;
    }
}

void flux_reactor_incref (flux_reactor_t *r)
{
    if (r)
        r->usecount++;
}

void flux_reactor_destroy (flux_reactor_t *r)
{
    flux_reactor_decref (r);
}

flux_reactor_t *flux_reactor_create (int flags)
{
    flux_reactor_t *r;

    if (flags != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(r = calloc (1, sizeof (*r))))
        return NULL;
#if HAVE_LIBUV
    int uverr;
    if ((uverr = uv_loop_init (&r->loop)) < 0) {
        free (r);
        errno = -uverr;
        return NULL;
    }
#else
    if (!(r->loop = ev_loop_new (EVFLAG_NOSIGMASK | EVFLAG_SIGNALFD))) {
        free (r);
        errno = ENOMEM;
        return NULL;
    }
    ev_set_userdata (r->loop, r);
#endif
    r->usecount = 1;
    return r;
}

int flux_reactor_run (flux_reactor_t *r, int flags)
{
    int count;
    int rflags;

    r->errflag = 0;
#if HAVE_LIBUV
    if (flags == FLUX_REACTOR_NOWAIT)
        rflags = UV_RUN_NOWAIT;
    else if (flags == FLUX_REACTOR_ONCE)
        rflags = UV_RUN_ONCE;
    else if (flags == 0)
        rflags = UV_RUN_DEFAULT;
    else
        goto error;
    count = uv_run (&r->loop, rflags);
#else
    if (flags == FLUX_REACTOR_NOWAIT)
        rflags = EVRUN_NOWAIT;
    else if (flags == FLUX_REACTOR_ONCE)
        rflags = EVRUN_ONCE;
    else if (flags == 0)
        rflags = 0;
    else
        goto error;
    count = ev_run (r->loop, rflags);
#endif
    return (r->errflag ? -1 : count);
error:
    errno = EINVAL;
    return -1;
}

double flux_reactor_time (void)
{
#if HAVE_LIBUV
    return 1E-9 * uv_hrtime();
#else
    return ev_time ();
#endif
}

double flux_reactor_now (flux_reactor_t *r)
{
#if HAVE_LIBUV
    return 1E-3 * uv_now (&r->loop);
#else
    return ev_now (r->loop);
#endif
}

void flux_reactor_now_update (flux_reactor_t *r)
{
#if HAVE_LIBUV
    return uv_update_time (&r->loop);
#else
    return ev_now_update (r->loop);
#endif
}

void flux_reactor_stop (flux_reactor_t *r)
{
    r->errflag = 0;
#if HAVE_LIBUV
    uv_stop (&r->loop);
#else
    ev_break (r->loop, EVBREAK_ALL);
#endif
}

void flux_reactor_stop_error (flux_reactor_t *r)
{
    r->errflag = 1;
#if HAVE_LIBUV
    uv_stop (&r->loop);
#else
    ev_break (r->loop, EVBREAK_ALL);
#endif
}

void *reactor_get_loop (flux_reactor_t *r)
{
    if (!r)
        return NULL;
#if HAVE_LIBUV
    return &r->loop;
#else
    return r->loop;
#endif
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
