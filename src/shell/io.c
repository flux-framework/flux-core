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
 * - leader takes a completion reference which it gives up once each
 *   task sends an EOF for both stdout and stderr.
 * - all shells (even the leader) send I/O to the service with RPC
 * - Any errors getting I/O to the leader are logged by RPC completion
 *   callbacks.
 * - Any outstanding RPCs at shell_io_destroy() are synchronously waited for
 *   there (checked for error, then destroyed).
 * - In standalone mode, the loop:// connector enables RPCs to work
 * - In standalone mode, output is written to the shell's stdout/stderr not KVS
 * - The number of in-flight write requests on each shell is limited to
 *   shell_io_hwm, to avoid matchtag exhaustion, etc. for chatty tasks.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libioencode/ioencode.h"

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
    bool stopped;
};

static const int shell_io_lwm = 100;
static const int shell_io_hwm = 1000;

/* Pause/resume output on 'stream' of 'task'.
 */
static void shell_io_control_task (struct shell_task *task,
                                   const char *stream,
                                   bool stop)
{
    if (stop) {
        if (flux_subprocess_stream_stop (task->proc, stream) < 0)
            log_err ("flux_subprocess_stream_stop %d:%s", task->rank, stream);
    }
    else {
        if (flux_subprocess_stream_start (task->proc, stream) < 0)
            log_err ("flux_subprocess_stream_start %d:%s", task->rank, stream);
    }
}

/* Pause/resume output for all tasks.
 */
static void shell_io_control (struct shell_io *io, bool stop)
{
    struct shell_task *task;

    if (io->stopped != stop) {
        task = zlist_first (io->shell->tasks);
        while (task) {
            shell_io_control_task (task, "stdout", stop);
            shell_io_control_task (task, "stderr", stop);
            task = zlist_next (io->shell->tasks);
        }
        io->stopped = stop;
    }
}

/* Convert 'iodecode' object to an valid RFC 24 data event.
 * N.B. the iodecode object is a valid "context" for the event.
 * io->output is a JSON array of eventlog entries.
 */
static void shell_io_write_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct shell_io *io = arg;
    bool eof = false;
    json_t *o;
    json_t *entry;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (iodecode (o, NULL, NULL, NULL, NULL, &eof) < 0)
        goto error;
    if (shell_svc_allowed (io->shell->svc, msg) < 0)
        goto error;
    if (!(entry = eventlog_entry_pack (0., "data", "O", o))) // increfs 'o'
        goto error;
    if (json_array_append_new (io->output, entry) < 0) {
        json_decref (entry);
        errno = ENOMEM;
        goto error;
    }
    if (eof) {
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

    if (zlist_size (io->pending_writes) <= shell_io_lwm)
        shell_io_control (io, false);
}

static int shell_io_write (struct shell_io *io,
                           int rank,
                           const char *stream,
                           const char *data,
                           int len,
                           bool eof)
{
    flux_future_t *f = NULL;
    json_t *o = NULL;

    if (!(o = ioencode (stream, rank, data, len, eof))) {
        log_err ("ioencode");
        return -1;
    }

    if (!(f = shell_svc_pack (io->shell->svc, "write", 0, 0, "O", o)))
        goto error;
    if (flux_future_then (f, -1, shell_io_write_completion, io) < 0)
        goto error;
    if (zlist_append (io->pending_writes, f) < 0)
        log_msg ("zlist_append failed");
    json_decref (o);

    if (zlist_size (io->pending_writes) >= shell_io_hwm)
        shell_io_control (io, true);
    return 0;

error:
    flux_future_destroy (f);
    json_decref (o);
    return -1;
}

static int shell_io_flush (struct shell_io *io)
{
    json_t *entry;
    size_t index;
    FILE *f;

    json_array_foreach (io->output, index, entry) {
        json_t *context;
        const char *name;
        const char *stream = NULL;
        int rank;
        char *data = NULL;
        int len = 0;
        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
            log_err ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "header")) {
            // TODO: acquire per-stream encoding type
        }
        else if (!strcmp (name, "data")) {
            if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
                log_err ("iodecode");
                return -1;
            }
            f = !strcmp (stream, "stdout") ? stdout : stderr;
            if (len > 0) {
                fprintf (f, "%d: ", rank);
                fwrite (data, len, 1, f);
            }
            free (data);
        }
    }
    return 0;
}

static int shell_io_commit (struct shell_io *io)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f = NULL;
    char *chunk;
    int saved_errno;
    int rc = -1;

    if (!(chunk = eventlog_encode (io->output)))
        return -1;
    if (!(txn = flux_kvs_txn_create ()))
        goto out;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "output", chunk) < 0)
        goto out;
    if (!(f = flux_kvs_commit (io->shell->h, NULL, 0, txn)))
        goto out;
    if (flux_future_get (f, NULL) < 0)
        goto out;
    rc = 0;
out:
    saved_errno = errno;
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (chunk);
    errno = saved_errno;
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

/* Append RFC 24 header event to 'output' JSON array.  Assume:
 * - fixed base64 encoding for stdout, stderr
 * - no options
 * - no stdlog
 */
static int shell_io_header_append (json_t *output)
{
    json_t *o;

    o = eventlog_entry_pack (0, "header",
                             "{s:i s:{s:s s:s} s:{}}",
                             "version", 1,
                             "encoding",
                               "stdout", "base64",
                               "stderr", "base64",
                             "options");
    if (!o || json_array_append_new (output, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
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
        if (shell_io_header_append (io->output) < 0)
            goto error;
    }
    return io;
error:
    shell_io_destroy (io);
    return NULL;
}

// shell_task_io_ready_f callback footprint
void shell_io_task_ready (struct shell_task *task, const char *stream, void *arg)
{
    struct shell_io *io = arg;
    const char *data;
    int len;

    data = flux_subprocess_getline (task->proc, stream, &len);
    if (len < 0) {
        log_err ("read %s task %d", stream, task->rank);
    }
    else if (len > 0) {
        if (shell_io_write (io, task->rank, stream, data, len, false) < 0)
            log_err ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_io_write (io, task->rank, stream, NULL, 0, true) < 0)
            log_err ("write eof %s task %d", stream, task->rank);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
