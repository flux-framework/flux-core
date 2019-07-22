/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* stdio handling
 *
 * Intercept task stdout, stderr and dispose of it according to
 * selected I/O mode.
 *
 * The leader shell implements an "shell-<id>.output" service that
 * all ranks send task output to.  Output objects accumulate in a json
 * array on the leader.  Upon task exit, the array is written to the
 * "output" key in the job's guest KVS namespace.
 *
 * Notes:
 * - EOF is indicated by an object with len = 0
 * - leader takes a completion reference which it gives up once each
 *   task sends an EOF for both stdout and stderr.
 * - all shells (even the leader) send I/O to the service with RPC
 * - Any errors getting I/O to the leader are logged by RPC completion
 *   callbacks.
 * - Any outstanding RPCs at shell_io_destroy() are synchronously waited for
 *   there (checked for error, then destroyed).
 * - In standalone mode, the loop:// connector enables RPCs to work
 * - In standalone mode, output is written to the shell's stdout/stderr not KVS
 * - Output data is encoded as JSON strings (not base64'ed).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#include "task.h"
#include "io.h"
#include "svc.h"
#include "shell.h"

struct shell_io {
    flux_shell_t *shell;
    int refcount;
    int eof_pending;
    zlist_t *pending_writes;
    json_t *output;
};

static void shell_io_write_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct shell_io *io = arg;
    int len;        // just decode len for EOF (len==0) detection
    json_t *o;      // decode output object for appending to io->output array

    if (flux_request_unpack (msg, NULL, "{s:i}", "len", &len) < 0)
        goto error;
    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (shell_svc_allowed (io->shell->svc, msg) < 0)
        goto error;
    if (json_array_append (io->output, o) < 0)
        goto error;
    if (len == 0) {
        if (--io->eof_pending == 0) {
            flux_msg_handler_stop (mh);
            if (flux_shell_remove_completion_ref (io->shell, "io-leader") < 0)
                log_err ("flux_shell_remove_completion_ref");
        }
    }
    if (flux_respond (io->shell->h, msg, NULL) < 0)
        log_err ("flux_respond");
    return;
error:
    if (flux_respond_error (io->shell->h, msg, errno, NULL) < 0)
        log_err ("flux_respond");
}

static void shell_io_write_completion (flux_future_t *f, void *arg)
{
    struct shell_io *io = arg;

    if (flux_future_get (f, NULL) < 0)
        log_err ("shell_io_write");
    zlist_remove (io->pending_writes, f);
    flux_future_destroy (f);
}

static int shell_io_write (struct shell_io *io,
                           int rank,
                           const char *name,
                           const char *data,
                           int len)
{
    flux_future_t *f;

    if (!(f = shell_svc_pack (io->shell->svc,
                             "write",
                             0,
                             0,
                             "{s:i s:s s:i s:s}",
                             "rank", rank,
                             "name", name,
                             "len", len,
                             "data", data)))
        return -1;
    if (flux_future_then (f, -1, shell_io_write_completion, io) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    if (zlist_append (io->pending_writes, f) < 0)
        log_msg ("zlist_append failed");
    return 0;
}

static int shell_io_flush (struct shell_io *io)
{
    json_t *entry;
    size_t index;
    int rank;
    const char *name;
    const char *data;
    int len;
    FILE *f;

    json_array_foreach (io->output, index, entry) {
        if (json_unpack (entry,
                         "{s:i s:s s:i s:s}",
                         "rank", &rank,
                         "name", &name,
                         "len", &len,
                         "data", &data) < 0)
            return -1;
        f = !strcmp (name, "STDOUT") ? stdout : stderr;
        if (len > 0) {
            fprintf (f, "%d: ", rank);
            fwrite (data, len, 1, f);
        }
    }
    return 0;
}

static int shell_io_commit (struct shell_io *io)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    if (flux_kvs_txn_pack (txn, 0, "output", "O", io->output) < 0)
        goto out;
    if (!(f = flux_kvs_commit (io->shell->h, NULL, 0, txn)))
        goto out;
    if (flux_future_get (f, NULL) < 0)
        goto out;
    rc = 0;
out:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return rc;
}

void shell_io_destroy (struct shell_io *io)
{
    if (io) {
        int saved_errno = errno;
        if (io->pending_writes) {
            flux_future_t *f;

            while ((f = zlist_pop (io->pending_writes))) { // leader+follower
                if (flux_future_get (f, NULL) < 0)
                    log_err ("shell_io_write");
                flux_future_destroy (f);
            }
            zlist_destroy (&io->pending_writes);
        }
        if (io->output) { // leader only
            if (io->shell->standalone) {
                if (shell_io_flush (io) < 0)
                    log_err ("shell_io_flush");
            }
            else {
                if (shell_io_commit (io) < 0)
                    log_err ("shell_io_commit");
            }
        }
        json_decref (io->output);
        free (io);
        errno = saved_errno;
    }
}

struct shell_io *shell_io_create (flux_shell_t *shell)
{
    struct shell_io *io;

    if (!(io = calloc (1, sizeof (*io))))
        return NULL;
    io->shell = shell;
    if (!(io->pending_writes = zlist_new ()))
        goto error;
    if (shell->info->shell_rank == 0) {
        if (shell_svc_register (shell->svc, "write", shell_io_write_cb, io) < 0)
            goto error;
        io->eof_pending = 2 * shell->info->jobspec->task_count;
        if (flux_shell_add_completion_ref (shell, "io-leader") < 0)
            goto error;
        if (!(io->output = json_array ())) {
            errno = ENOMEM;
            goto error;
        }
    }
    return io;
error:
    shell_io_destroy (io);
    return NULL;
}

// shell_task_io_ready_f callback footprint
void shell_io_task_ready (struct shell_task *task, const char *name, void *arg)
{
    struct shell_io *io = arg;
    const char *data;
    int len;

    len = shell_task_io_readline (task, name, &data);
    if (len < 0) {
        log_err ("read %s task %d", name, task->rank);
    }
    else if (len > 0 || (len == 0 && shell_task_io_at_eof (task, name))) {
        if (shell_io_write (io, task->rank, name, data, len) < 0)
            log_err ("write %s task %d", name, task->rank);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
