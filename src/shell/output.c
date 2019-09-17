/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* std output handling
 *
 * Intercept task stdout, stderr and dispose of it according to
 * selected I/O mode.
 *
 * If output goes to terminal or stdout/stderr is written to the KVS,
 * the leader shell implements an "shell-<id>.output" service that all
 * ranks send task output to.  Output objects accumulate in a json
 * array on the leader.  Depending on settings, output is written
 * directly to stdout/stderr or output objects are written to the
 * "output" key in the job's guest KVS namespace per RFC24.
 *
 * Notes:
 * - leader takes a completion reference which it gives up once each
 *   task sends an EOF for both stdout and stderr.
 * - completion reference also taken for each KVS commit, to ensure
 *   commits complete before shell exits
 * - all shells (even the leader) send I/O to the service with RPC
 * - Any errors getting I/O to the leader are logged by RPC completion
 *   callbacks.
 * - Any outstanding RPCs at shell_output_destroy() are synchronously waited for
 *   there (checked for error, then destroyed).
 * - In standalone mode, the loop:// connector enables RPCs to work
 * - In standalone mode, output is written to the shell's stdout/stderr not KVS
 * - The number of in-flight write requests on each shell is limited to
 *   shell_output_hwm, to avoid matchtag exhaustion, etc. for chatty tasks.
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
#include "svc.h"
#include "internal.h"
#include "builtins.h"

enum {
    FLUX_OUTPUT_TYPE_TERM = 1,
    FLUX_OUTPUT_TYPE_KVS = 2,
    FLUX_OUTPUT_TYPE_FILE = 3,
};

struct shell_output_file {
    const char *path;
    int fd;
};

struct shell_output {
    flux_shell_t *shell;
    int refcount;
    int eof_pending;
    zlist_t *pending_writes;
    json_t *output;
    bool stopped;
    int stdout_type;
    int stderr_type;
    struct shell_output_file stdout_file;
    struct shell_output_file stderr_file;
};

static const int shell_output_lwm = 100;
static const int shell_output_hwm = 1000;

/* Pause/resume output on 'stream' of 'task'.
 */
static void shell_output_control_task (struct shell_task *task,
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
static void shell_output_control (struct shell_output *out, bool stop)
{
    struct shell_task *task;

    if (out->stopped != stop) {
        task = zlist_first (out->shell->tasks);
        while (task) {
            shell_output_control_task (task, "stdout", stop);
            shell_output_control_task (task, "stderr", stop);
            task = zlist_next (out->shell->tasks);
        }
        out->stopped = stop;
    }
}

static int shell_output_term_init (struct shell_output *out, json_t *header)
{
    // TODO: acquire per-stream encoding type
    return 0;
}

static int shell_output_term (struct shell_output *out)
{
    json_t *entry;
    size_t index;

    json_array_foreach (out->output, index, entry) {
        json_t *context;
        const char *name;
        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
            log_err ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "data")) {
            int output_type;
            FILE *f;
            const char *stream = NULL;
            int rank;
            char *data = NULL;
            int len = 0;
            if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
                log_err ("iodecode");
                return -1;
            }
            if (!strcmp (stream, "stdout")) {
                output_type = out->stdout_type;
                f = stdout;
            }
            else {
                output_type = out->stderr_type;
                f = stderr;
            }
            if ((output_type == FLUX_OUTPUT_TYPE_TERM) && len > 0) {
                fprintf (f, "%d: ", rank);
                fwrite (data, len, 1, f);
            }
            free (data);
        }
    }
    return 0;
}

/* log entry to exec.eventlog the type of output we're doing, and that
 * we've created the output directory */
static int eventlog_append (flux_kvs_txn_t *txn,
                            const char *name,
                            const char *fmt, ...)
{
    const char *key = "exec.eventlog";
    json_t *entry = NULL;
    char *entrystr = NULL;
    va_list ap;
    int saved_errno, rc = -1;

    va_start (ap, fmt);

    if (!(entry = eventlog_entry_pack (0.,
                                       name,
                                       "{s:s}",
                                       "type", "kvs"))) {
        log_err ("eventlog_entry_create");
        goto error;
    }
    if (!(entrystr = eventlog_entry_encode (entry))) {
        log_err ("eventlog_entry_encode");
        goto error;
    }
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, entrystr) < 0) {
        log_err ("flux_kvs_txn_put");
        goto error;
    }
    rc = 0;
error:
    /* on error, future destroyed via shell_output destroy */
    saved_errno = errno;
    va_end (ap);
    json_decref (entry);
    free (entrystr);
    errno = saved_errno;
    return rc;
}

/* check if this output type requires the service to be started */
static bool output_type_requires_service (int type)
{
    if ((type == FLUX_OUTPUT_TYPE_TERM)
        || (type == FLUX_OUTPUT_TYPE_KVS)
        || (type == FLUX_OUTPUT_TYPE_FILE))
        return true;
    return false;
}

static const char *output_type_str (int type)
{
    switch (type) {
    case FLUX_OUTPUT_TYPE_TERM:
        return "term";
    case FLUX_OUTPUT_TYPE_KVS:
        return "kvs";
    case FLUX_OUTPUT_TYPE_FILE:
        return "file";
    }
    log_err_exit ("output type invalid: %d", type);
}

static int shell_output_eventlog (struct shell_output *out, flux_kvs_txn_t *txn)
{
    const char *type;
    type = output_type_str (out->stdout_type);
    if (eventlog_append (txn, "output-stdout", "{s:s}", "type", type) < 0)
        return -1;
    type = output_type_str (out->stderr_type);
    if (eventlog_append (txn, "output-stderr", "{s:s}", "type", type) < 0)
        return -1;
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS)
        || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS)) {
        if (eventlog_append (txn, "output-kvs-ready", NULL) < 0)
            return -1;
    }
    return 0;
}

static void shell_output_kvs_init_completion (flux_future_t *f, void *arg)
{
    struct shell_output *out = arg;

    if (flux_future_get (f, NULL) < 0)
        /* failng to commit output-kvs-ready or header is a fatal
         * error.  Should be cleaner in future. Issue #2378 */
        log_err_exit ("shell_output_kvs_init");
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (out->shell, "output.kvs-init") < 0)
        log_err ("flux_shell_remove_completion_ref");
}

static int shell_output_kvs_init (struct shell_output *out, json_t *header)
{
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    char *headerstr = NULL;
    int saved_errno;
    int rc = -1;

    if (!(headerstr = eventlog_entry_encode (header)))
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "output", headerstr) < 0)
        goto error;
    if (shell_output_eventlog (out, txn) < 0)
        goto error;
    if (!(f = flux_kvs_commit (out->shell->h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (f, -1, shell_output_kvs_init_completion, out) < 0)
        goto error;
    if (flux_shell_add_completion_ref (out->shell, "output.kvs-init") < 0) {
        log_err ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of shell_output_kvs_init_completion()
     * callback */
    f = NULL;
    rc = 0;
error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (headerstr);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

static void shell_output_kvs_completion (flux_future_t *f, void *arg)
{
    struct shell_output *out = arg;

    /* Error failing to commit is a fatal error.  Should be cleaner in
     * future. Issue #2378 */
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("shell_output_kvs");
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (out->shell, "output.kvs") < 0)
        log_err ("flux_shell_remove_completion_ref");
}

static int shell_output_kvs (struct shell_output *out)
{
    json_t *entry;
    size_t index;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    int saved_errno;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    json_array_foreach (out->output, index, entry) {
        json_t *context;
        const char *name;
        const char *stream = NULL;
        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
            log_err ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "data")) {
            int output_type;
            if (iodecode (context, &stream, NULL, NULL, NULL, NULL) < 0) {
                log_err ("iodecode");
                return -1;
            }
            if (!strcmp (stream, "stdout"))
                output_type = out->stdout_type;
            else
                output_type = out->stderr_type;
            if (output_type == FLUX_OUTPUT_TYPE_KVS) {
                char *entrystr = eventlog_entry_encode (entry);
                if (!entrystr) {
                    log_err ("eventlog_entry_encode");
                    goto error;
                }
                if (flux_kvs_txn_put (txn,
                                      FLUX_KVS_APPEND,
                                      "output",
                                      entrystr) < 0) {
                    free (entrystr);
                    goto error;
                }
                free (entrystr);
            }
        }
    }
    if (!(f = flux_kvs_commit (out->shell->h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (f, -1, shell_output_kvs_completion, out) < 0)
        goto error;
    if (flux_shell_add_completion_ref (out->shell, "output.kvs") < 0) {
        log_err ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of shell_output_kvs_completion()
     * callback */
    f = NULL;
    rc = 0;
error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

static int shell_output_fd (int fd, void *buf, size_t len)
{
    size_t count = 0;
    int n = 0;
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0) {
            if (errno != EINTR)
                return -1;
            continue;
        }
        count += n;
    }
    return n;
}

static int shell_output_file (struct shell_output *out)
{
    json_t *entry;
    size_t index;

    json_array_foreach (out->output, index, entry) {
        json_t *context;
        const char *name;
        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
            log_err ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "data")) {
            struct shell_output_file *sof;
            int output_type;
            const char *stream = NULL;
            int rank;
            char *data = NULL;
            int len = 0;
            if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
                log_err ("iodecode");
                return -1;
            }
            if (!strcmp (stream, "stdout")) {
                output_type = out->stdout_type;
                sof = &out->stdout_file;
            }
            else {
                output_type = out->stderr_type;
                sof = &out->stderr_file;
            }
            if ((output_type == FLUX_OUTPUT_TYPE_FILE) && len > 0) {
                if (shell_output_fd (sof->fd, data, len) < 0)
                    return -1;
            }
            free (data);
        }
    }
    return 0;
}

/* Convert 'iodecode' object to an valid RFC 24 data event.
 * N.B. the iodecode object is a valid "context" for the event.
 */
static void shell_output_write_cb (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct shell_output *out = arg;
    bool eof = false;
    json_t *o;
    json_t *entry;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (iodecode (o, NULL, NULL, NULL, NULL, &eof) < 0)
        goto error;
    if (shell_svc_allowed (out->shell->svc, msg) < 0)
        goto error;
    if (!(entry = eventlog_entry_pack (0., "data", "O", o))) // increfs 'o'
        goto error;
    if (json_array_append_new (out->output, entry) < 0) {
        json_decref (entry);
        errno = ENOMEM;
        goto error;
    }
    /* Error failing to commit is a fatal error.  Should be cleaner in
     * future. Issue #2378 */
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_TERM
         || (out->stderr_type == FLUX_OUTPUT_TYPE_TERM))) {
        if (shell_output_term (out) < 0)
            log_err_exit ("shell_output_term");
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS
         || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS))) {
        if (shell_output_kvs (out) < 0)
            log_err_exit ("shell_output_kvs");
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_FILE
         || (out->stderr_type == FLUX_OUTPUT_TYPE_FILE))) {
        if (shell_output_file (out) < 0)
            log_err ("shell_output_file");
    }
    if (json_array_clear (out->output) < 0) {
        log_msg ("json_array_clear failed");
        goto error;
    }
    if (eof) {
        if (--out->eof_pending == 0) {
            flux_msg_handler_stop (mh);
            if (flux_shell_remove_completion_ref (out->shell, "output.write") < 0)
                log_err ("flux_shell_remove_completion_ref");
        }
    }
    if (flux_respond (out->shell->h, msg, NULL) < 0)
        log_err ("flux_respond");
    return;
error:
    if (flux_respond_error (out->shell->h, msg, errno, NULL) < 0)
        log_err ("flux_respond");
}

static void shell_output_write_completion (flux_future_t *f, void *arg)
{
    struct shell_output *out = arg;

    if (flux_future_get (f, NULL) < 0)
        log_err ("shell_output_write");
    zlist_remove (out->pending_writes, f);
    flux_future_destroy (f);

    if (zlist_size (out->pending_writes) <= shell_output_lwm)
        shell_output_control (out, false);
}

static int shell_output_write (struct shell_output *out,
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

    if (!(f = flux_shell_rpc_pack (out->shell, "write", 0, 0, "O", o)))
        goto error;
    if (flux_future_then (f, -1, shell_output_write_completion, out) < 0)
        goto error;
    if (zlist_append (out->pending_writes, f) < 0)
        log_msg ("zlist_append failed");
    json_decref (o);

    if (zlist_size (out->pending_writes) >= shell_output_hwm)
        shell_output_control (out, true);
    return 0;

error:
    flux_future_destroy (f);
    json_decref (o);
    return -1;
}

static void shell_output_file_cleanup (struct shell_output_file *sof)
{
    if (sof->fd != -1)
        close (sof->fd);
}

void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;
        if (out->pending_writes) {
            flux_future_t *f;

            while ((f = zlist_pop (out->pending_writes))) { // leader+follower
                if (flux_future_get (f, NULL) < 0)
                    log_err ("shell_output_write");
                flux_future_destroy (f);
            }
            zlist_destroy (&out->pending_writes);
        }
        if (out->output && json_array_size (out->output) > 0) { // leader only
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_TERM)
                || (out->stderr_type == FLUX_OUTPUT_TYPE_TERM)) {
                if (shell_output_term (out) < 0)
                    log_err ("shell_output_term");
            }
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS)
                || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS)) {
                if (shell_output_kvs (out) < 0)
                    log_err ("shell_output_kvs");
            }
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_FILE
                 || (out->stderr_type == FLUX_OUTPUT_TYPE_FILE))) {
                if (shell_output_file (out) < 0)
                    log_err ("shell_output_file");
            }
        }
        json_decref (out->output);
        shell_output_file_cleanup (&out->stdout_file);
        shell_output_file_cleanup (&out->stderr_file);
        free (out);
        errno = saved_errno;
    }
}

static void shell_output_file_init (struct shell_output_file *sof)
{
    sof->path = NULL;
    sof->fd = -1;
}

static int shell_output_parse_type (struct shell_output *out,
                                    const char *stream,
                                    int *typep,
                                    struct shell_output_file *sof)
{
    const char *typestr = NULL;
    int ret;

    if ((ret = flux_shell_getopt_unpack (out->shell, "output",
                                         "{s?:{s?:s}}",
                                         stream, "type", &typestr)) < 0)
        return -1;

    if (!ret || !typestr)
        return 0;

    if (!strcmp (typestr, "kvs"))
        (*typep) = FLUX_OUTPUT_TYPE_KVS;
    else if (!strcmp (typestr, "file")) {
        (*typep) = FLUX_OUTPUT_TYPE_FILE;

        if (flux_shell_getopt_unpack (out->shell, "output",
                                      "{s:{s?:s}}",
                                      stream, "path", &(sof->path)) < 0)
            return -1;

        if (sof->path == NULL) {
            log_msg ("path for %s file output not specified", stream);
            return -1;
        }
    }
    else {
        log_msg ("invalid output type specified '%s'", typestr);
        return -1;
    }
    return 0;
}

static int shell_output_setup_file (struct shell_output *out,
                                    struct shell_output_file *sof)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int open_flags = O_CREAT | O_TRUNC | O_WRONLY;
    int fd = -1;

    if ((fd = open (sof->path, open_flags, mode)) < 0) {
        log_err ("error opening output file '%s'", sof->path);
        return -1;
    }

    sof->fd = fd;
    return 0;
}

/* Append RFC 24 header event to 'output' JSON array and write out to
 * KVS.  Assume:
 * - fixed base64 encoding for stdout, stderr
 * - no options
 * - no stdlog
 */
static int shell_output_header (struct shell_output *out)
{
    json_t *o = NULL;
    int rc = -1;

    o = eventlog_entry_pack (0, "header",
                             "{s:i s:{s:s s:s} s:{s:i s:i} s:{}}",
                             "version", 1,
                             "encoding",
                               "stdout", "base64",
                               "stderr", "base64",
                             "count",
                               "stdout", out->shell->info->jobspec->task_count,
                               "stderr", out->shell->info->jobspec->task_count,
                             "options");
    if (!o) {
        errno = ENOMEM;
        goto error;
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_TERM)
        || (out->stderr_type == FLUX_OUTPUT_TYPE_TERM)) {
        if (shell_output_term_init (out, o) < 0)
            log_err ("shell_output_term_init");
    }
    /* will also emit necessary entries to exec.eventlog.  Call this
     * as long as we're not standalone.  Since we minimally want to
     * log output type to the eventlog.
     */
    if (!out->shell->standalone) {
        if (shell_output_kvs_init (out, o) < 0)
            log_err ("shell_output_kvs_init");
    }
    rc = 0;
error:
    json_decref (o);
    return rc;
}

struct shell_output *shell_output_create (flux_shell_t *shell)
{
    struct shell_output *out;

    if (!(out = calloc (1, sizeof (*out))))
        return NULL;
    out->shell = shell;
    shell_output_file_init (&out->stdout_file);
    shell_output_file_init (&out->stderr_file);
    if (out->shell->standalone) {
        out->stdout_type = FLUX_OUTPUT_TYPE_TERM;
        out->stderr_type = FLUX_OUTPUT_TYPE_TERM;
    }
    else {
        out->stdout_type = FLUX_OUTPUT_TYPE_KVS;
        out->stderr_type = FLUX_OUTPUT_TYPE_KVS;
    }

    /* Check if user specified alternate shell output */
    if (shell_output_parse_type (out,
                                 "stdout",
                                 &(out->stdout_type),
                                 &(out->stdout_file)) < 0)
        goto error;

    if (shell_output_parse_type (out,
                                 "stderr",
                                 &(out->stderr_type),
                                 &(out->stderr_file)) < 0)
        goto error;

    if (!(out->pending_writes = zlist_new ()))
        goto error;
    if (shell->info->shell_rank == 0) {
        if (output_type_requires_service (out->stdout_type)
            || output_type_requires_service (out->stderr_type)) {
            if (flux_shell_service_register (shell,
                                             "write",
                                             shell_output_write_cb,
                                             out) < 0)
                goto error;
            if (output_type_requires_service (out->stdout_type))
                out->eof_pending += shell->info->jobspec->task_count;
            if (output_type_requires_service (out->stderr_type))
                out->eof_pending += shell->info->jobspec->task_count;
            if (flux_shell_add_completion_ref (shell, "output.write") < 0)
                goto error;
            if (!(out->output = json_array ())) {
                errno = ENOMEM;
                goto error;
            }
        }
        if (shell_output_header (out) < 0)
            goto error;
        if (out->stdout_type == FLUX_OUTPUT_TYPE_FILE) {
            if (shell_output_setup_file (out, &(out->stdout_file)) < 0)
                goto error;
        }
        if (out->stderr_type == FLUX_OUTPUT_TYPE_FILE) {
            if (shell_output_setup_file (out, &(out->stderr_file)) < 0)
                goto error;
        }
    }
    return out;
error:
    shell_output_destroy (out);
    return NULL;
}

static void task_output_cb (struct shell_task *task,
                            const char *stream,
                            void *arg)
{
    struct shell_output *out = arg;
    const char *data;
    int len;

    data = flux_subprocess_getline (task->proc, stream, &len);
    if (len < 0) {
        log_err ("read %s task %d", stream, task->rank);
    }
    else if (len > 0) {
        if (shell_output_write (out, task->rank, stream, data, len, false) < 0)
            log_err ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_output_write (out, task->rank, stream, NULL, 0, true) < 0)
            log_err ("write eof %s task %d", stream, task->rank);
    }
}

static int shell_output_task_init (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_output *out = flux_plugin_aux_get (p, "builtin.output");
    flux_shell_task_t *task;

    if (!shell || !out || !(task = flux_shell_current_task (shell)))
        return -1;

    if (output_type_requires_service (out->stdout_type)) {
        if (flux_shell_task_channel_subscribe (task, "stdout",
                                               task_output_cb, out) < 0)
            return -1;
    }
    if (output_type_requires_service (out->stderr_type)) {
        if (flux_shell_task_channel_subscribe (task, "stderr",
                                               task_output_cb, out) < 0)
            return -1;
    }

    return 0;
}

static int shell_output_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct shell_output *out = shell_output_create (shell);
    if (!out)
        return -1;
    if (flux_plugin_aux_set (p, "builtin.output", out,
                            (flux_free_f) shell_output_destroy) < 0) {
        shell_output_destroy (out);
        return -1;
    }
    return 0;
}

struct shell_builtin builtin_output = {
    .name = "output",
    .init = shell_output_init,
    .task_init = shell_output_task_init
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
