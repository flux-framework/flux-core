/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* MPIR_proctable service for job shell
 *
 */
#define FLUX_SHELL_PLUGIN_NAME "mpir"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <jansson.h>

#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "builtins.h"
#include "mpir/proctable.h"


/*  Structure for use during proctable gather from shell rank 0.
 */
struct proctable_gather {
    flux_t *h;
    flux_shell_t *shell;
    int shell_size;
    const flux_msg_t *req;
    zlistx_t *proctables;
    zlistx_t *futures;
};

/*  This node's hostname
 */
static char hostname [1024] = "";

static int shell_task_rank (flux_shell_task_t *task)
{
    int rank = -1;
    if (flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return -1;
    return rank;
}

static int shell_size (flux_shell_t *shell)
{
    int size = -1;
    if (flux_shell_info_unpack (shell, "{s:i}", "size", &size) < 0)
        return -1;
    return size;
}

static int shell_rank (flux_shell_t *shell)
{
    int rank = -1;
    if (flux_shell_info_unpack (shell, "{s:i}", "rank", &rank) < 0)
        return -1;
    return rank;
}

static int proctable_add_task (struct proctable *p,
                               int broker_rank,
                               flux_shell_task_t *task)
{
    flux_subprocess_t *proc;
    flux_cmd_t *cmd;
    int rank;

    if (hostname[0] == '\0'
        && gethostname (hostname, 1024) < 0) {
        shell_log_errno ("gethostname");
        return -1;
    }

    if (!(proc = flux_shell_task_subprocess (task))
        || !(cmd = flux_subprocess_get_cmd (proc))
        || (rank = shell_task_rank (task)) < 0) {
        shell_log_errno ("failed to get subprocess/cmd/rank");
        return -1;
    }

    if (proctable_append_task (p,
                               broker_rank,
                               hostname,
                               flux_cmd_arg (cmd, 0),
                               rank,
                               flux_subprocess_pid (proc)) < 0) {
        shell_log_errno ("proctable_append_task");
        return -1;
    }
    return 0;
}

static struct proctable * local_proctable_create (flux_shell_t *shell)
{
    flux_shell_task_t *task;
    int broker_rank;
    struct proctable *p;

    if (flux_shell_rank_info_unpack (shell,
                                     -1,
                                     "{s:i}",
                                     "broker_rank", &broker_rank) < 0) {
        shell_log_errno ("failed to get broker rank of current shell");
        return NULL;
    }

    if (!(p = proctable_create ()))
        return NULL;
    if (!(task = flux_shell_task_first (shell)))
        shell_log_errno ("No tasks?!");
    while (task) {
        if (proctable_add_task (p, broker_rank, task) < 0)
            goto err;
        task = flux_shell_task_next (shell);
    }
    return p;
err:
    proctable_destroy (p);
    return NULL;
}

static int respond_proctable (flux_t *h,
                              const flux_msg_t *msg,
                              struct proctable *p)
{
    int rc = -1;
    char *s = NULL;
    json_t *o = proctable_to_json (p);

    if (!o || !(s = json_dumps (o, JSON_COMPACT)))
        goto out;
    if ((rc = flux_respond (h, msg, s)) < 0)
        shell_log_errno ("respond_proctable");
out:
    free (s);
    json_decref (o);
    return rc;
}

/*  zlistx_set_comparator prototype
 *  sort proctables by lowest taskid first.
 */
static int proctable_cmp (const void *item1, const void *item2)
{
    const struct proctable *p1 = item1;
    const struct proctable *p2 = item2;
    int x = proctable_first_task (p1);
    int y = proctable_first_task (p2);
    return x == y ? 0 : x < y ? -1 : 1;
}

/*  zlistx_set_destructor prototype */
static void proctable_free (void **item)
{
    if (item) {
        struct proctable *p = *item;
        proctable_destroy (p);
        *item = NULL;
    }
}

/*  zlistx_set_destructor prototype */
static void future_free (void **item)
{
    if (item) {
        flux_future_t *f = *item;
        flux_future_destroy (f);
        *item = NULL;
    }
}

static void proctable_gather_destroy (struct proctable_gather *pg)
{
    if (pg) {
        flux_shell_remove_completion_ref (pg->shell, "proctable.get");
        if (pg->proctables)
            zlistx_destroy (&pg->proctables);
        if (pg->futures)
            zlistx_destroy (&pg->futures);
        flux_msg_decref (pg->req);
        free (pg);
    }
}

static struct proctable_gather *proctable_gather_create (flux_shell_t *shell,
                                                         int shell_size,
                                                         const flux_msg_t *msg)
{
    struct proctable_gather *pg = calloc (1, sizeof (*pg));
    if (!pg)
        return NULL;
    pg->shell = shell;

    /*  Hold a completion reference while the proctable gather operation
     *   is active:
     */
    flux_shell_add_completion_ref (pg->shell, "proctable.get");

    pg->shell_size = shell_size;
    pg->h = flux_shell_get_flux (shell);
    pg->req = flux_msg_incref (msg);
    if (!(pg->proctables = zlistx_new ())
        || !(pg->futures = zlistx_new ()))
        goto err;
    zlistx_set_comparator (pg->proctables, proctable_cmp);
    zlistx_set_destructor (pg->proctables, proctable_free);
    zlistx_set_destructor (pg->futures, future_free);

    return pg;
err:
    proctable_gather_destroy (pg);
    return NULL;
}

static int proctable_gather_complete (struct proctable_gather *pg)
{
    /*  Once we've stored all local/remote proctables, reduce the
     *   list and respond to original request.
     */
    if (zlistx_size (pg->proctables) == pg->shell_size) {
        struct proctable *p = zlistx_detach (pg->proctables, NULL);
        struct proctable *next = zlistx_detach (pg->proctables, NULL);

        /*  Append each proctable in order to the first proctable 'p'
         *   proctable_append_proctable_destroy () consumes the 2nd
         *   argument, so pop all proctables off the list as they are
         *   appended.
         */
        while (next) {
            if (proctable_append_proctable_destroy (p, next) < 0)
                return shell_log_errno ("proctable_append");
            next = zlistx_detach (pg->proctables, NULL);
        }
        shell_debug ("proctable gather complete. size=%d",
                     proctable_get_size (p));

        /*  Respond to the original request with the full proctable:
         */
        if (respond_proctable (pg->h, pg->req, p) < 0)
            return shell_log_errno ("proctable respond");

        proctable_destroy (p);
        proctable_gather_destroy (pg);
    }
    return 0;
}

static void proctable_gather_cancel (struct proctable_gather *pg)
{
    /*  Notify requester that we couldn't gather all proctables.
     *  Most likely this is due to a race with job exit.
     */
    if (flux_respond_error (pg->h, pg->req, ECANCELED, NULL) < 0)
        shell_log_errno ("flux_response");
    proctable_gather_destroy (pg);
}

static int proctable_gather_insert (struct proctable_gather *pg,
                                    struct proctable *p)
{
    /*  Insert one proctable into sorted position on the proctables list.
     *   Then, check to determine if all proctables have been received
     *   and send response.
     */
    if (!zlistx_insert (pg->proctables, p, false))
        return -1;
    return proctable_gather_complete (pg);
}

static void proctable_get_cb (flux_future_t *f, void *arg)
{
    struct proctable_gather *pg = arg;
    struct proctable *p;
    json_t *o;

    if (flux_rpc_get_unpack (f, "o", &o) < 0) {
        shell_log_errno ("proctable_get");
        goto err;
    }
    if (!(p = proctable_from_json (o))) {
        shell_log_errno ("proctable_from_json");
        goto err;
    }
    if (proctable_gather_insert (pg, p) < 0) {
        shell_log_errno ("proctable_gather_insert");
        goto err;
    }
    return;
err:
    proctable_gather_cancel (pg);
}

static int request_all_proctables (flux_shell_t *shell,
                                   int shell_size,
                                   const flux_msg_t *msg,
                                   struct proctable *p)
{
    struct proctable_gather *pg;

    if (!(pg = proctable_gather_create (shell, shell_size, msg))
        || !zlistx_insert (pg->proctables, p, false)) {
        shell_log_errno ("failed to create proctable gather struct");
        goto err;
    }

    shell_debug ("requesting proctables from %d ranks", pg->shell_size - 1);
    for (int i = 1; i < pg->shell_size; i++) {
        flux_future_t *f;
        /*  Request proctable from remote shell:
         */
        if (!(f = flux_shell_rpc_pack (shell, "proctable", i, 0, "{}"))) {
            shell_log_errno ("flux_shell_rpc_pack");
            goto err;
        }
        /*  Give 5.0s for shells to respond. This timeout is required
         *   in case remote shells have already exited or are exiting
         *   at the time the leader shell requests proctables. In that
         *   case the request RPC is dropped without any ENOSYS response.
         */
        if (flux_future_then (f, 5., proctable_get_cb, pg) < 0) {
            shell_log_errno ("flux_future_then");
            goto err;
        }
        zlistx_add_end (pg->futures, f);
    }
    return 0;
err:
    proctable_gather_destroy (pg);
    return -1;
}

static void mpir_proctable_get (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    flux_shell_t *shell = arg;
    int size = shell_size (shell);
    struct proctable *p = local_proctable_create (shell);

    if (p == NULL)
        goto error;

    /*  For non-leader shells, or a job size of 1, immediately respond
     *   to the request with the local proctable.
     */
    if (shell_rank (shell) != 0 || size == 1) {
        if (respond_proctable (h, msg, p) < 0)
            shell_log_errno ("unable to send proctable");
        proctable_destroy (p);
        return;
    }

    /*  On a leader shell in a job with size > 1, initiate requests to
     *   all other shells for their local proctables.
     */
    if (request_all_proctables (shell, size, msg, p) < 0)
        shell_log_errno ("request_all_proctables");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_response");
}

static int mpir_service_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!shell)
        return -1;

    /*  Register the `shell-<id>.proctable` service.
     *  All shells in a job implement this service, but only the leader
     *   shell will build the full MPIR_proctable.
     */
    if (flux_shell_service_register (shell,
                                     "proctable",
                                     mpir_proctable_get,
                                     shell) < 0)
        shell_die (1, "flux_shell_service_register()");
    return 0;
}

struct shell_builtin builtin_mpir = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = mpir_service_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
