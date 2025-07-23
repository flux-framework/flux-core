/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/wait.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"

#include "sigchld.h"

struct sigchld_proc {
    pid_t pid;
    sigchld_f cb;
    void *arg;
    flux_watcher_t *w; // dummy watcher for reactor ref
};

struct sigchld_ctx {
    zhashx_t *procs;   // pid => sigchld_proc
    flux_watcher_t *w; // SIGCHLD watcher
    int refcount;
};

static struct sigchld_ctx *sigchld_ctx;

static size_t proc_hasher (const void *key)
{
    const pid_t *pid = key;
    return *pid;
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

static int proc_key_cmp (const void *key1, const void *key2)
{
    const pid_t *pid1 = key1;
    const pid_t *pid2 = key2;

    return NUMCMP (*pid1, *pid2);
}

static void proc_destructor (struct sigchld_proc **p)
{
    if (p && *p) {
        int saved_errno = errno;
        flux_watcher_destroy ((*p)->w); // drop reactor reference
        free (*p);
        *p = NULL;
        errno = saved_errno;
    }
}

static struct sigchld_proc *proc_create (flux_reactor_t *r,
                                         pid_t pid,
                                         sigchld_f cb,
                                         void *arg)
{
    struct sigchld_proc *p;

    if (!(p = calloc (1, sizeof (*p))))
        return NULL;
    if (!(p->w = flux_prepare_watcher_create (r, NULL, NULL)))
        goto error;
    flux_watcher_start (p->w); // take reactor reference
    p->pid = pid;
    p->cb = cb;
    p->arg = arg;
    return p;
error:
    proc_destructor (&p);
    return NULL;
}

static void sigchld_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    pid_t pid;
    int status;

    if (!sigchld_ctx)
        return;

    sigchld_initialize (r); // incref ctx
    do {
        pid = waitpid (-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid > 0) {
            struct sigchld_proc *p;
            if ((p = zhashx_lookup (sigchld_ctx->procs, &pid)))
                p->cb (pid, status, p->arg);
        }
    } while (pid > 0 || (pid == -1 && errno == EINTR));
    sigchld_finalize (); // decref ctx
}

int sigchld_register (flux_reactor_t *r, pid_t pid, sigchld_f cb, void *arg)
{
    struct sigchld_proc *p;

    if (!sigchld_ctx || pid <= 0 || cb == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!(p = proc_create (r, pid, cb, arg)))
        return -1;
    if (zhashx_insert (sigchld_ctx->procs, &p->pid, p) < 0) {
        errno = ENOMEM;
        proc_destructor (&p);
        return -1;
    }
    return 0;
}

void sigchld_unregister (pid_t pid)
{
    if (sigchld_ctx)
        zhashx_delete (sigchld_ctx->procs, &pid);
}

static void sigchld_ctx_destroy (struct sigchld_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_watcher_destroy (ctx->w);
        zhashx_destroy (&ctx->procs);
        free (ctx);
        errno = saved_errno;
    }
}

static struct sigchld_ctx *sigchld_ctx_create (flux_reactor_t *r)
{
    struct sigchld_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->refcount = 1;
    if (!(ctx->procs = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_key_hasher (ctx->procs, proc_hasher);
    zhashx_set_key_comparator (ctx->procs, proc_key_cmp);
    zhashx_set_key_duplicator (ctx->procs, NULL);
    zhashx_set_key_destructor (ctx->procs, NULL);
    zhashx_set_destructor (ctx->procs, (zhashx_destructor_fn *)proc_destructor);

    if (!(ctx->w = flux_signal_watcher_create (r, SIGCHLD, sigchld_cb, NULL)))
        goto error;
    flux_watcher_unref (ctx->w);
    flux_watcher_start (ctx->w);
    return ctx;
error:
    sigchld_ctx_destroy (ctx);
    return NULL;
}

void sigchld_finalize (void)
{
    if (sigchld_ctx && --sigchld_ctx->refcount == 0) {
        sigchld_ctx_destroy (sigchld_ctx);
        sigchld_ctx = NULL;
    }
}

int sigchld_initialize (flux_reactor_t *r)
{
    if (!sigchld_ctx) {
        if (!(sigchld_ctx = sigchld_ctx_create (r)))
            return -1;
    }
    else
        sigchld_ctx->refcount++;
    return 0;
}

// vi:ts=4 sw=4 expandtab
