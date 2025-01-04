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
 * If output is written to the KVS or directly to a file, the leader shell
 * implements an "shell-<id>.output" service that all ranks send task
 * output to.  Output objects accumulate in a json array on the
 * leader.  Depending on settings, output is written directly to
 * stdout/stderr, output objects are written to the "output" key in
 * the job's guest KVS namespace per RFC24, or output is written to a
 * configured file.
 *
 * Notes:
 * - leader takes a completion reference which it gives up once each
 *   task sends an EOF for both stdout and stderr.
 * - completion reference also taken for each KVS commit, to ensure
 *   commits complete before shell exits
 * - follower shells send I/O to the service with RPC
 * - Any errors getting I/O to the leader are logged by RPC completion
 *   callbacks.
 * - Any outstanding RPCs at shell_output_destroy() are synchronously waited for
 *   there (checked for error, then destroyed).
 * - Any outstanding file writes at shell_output_destroy() are
 *   synchronously waited for to complete.
 * - The number of in-flight write requests on each shell is limited to
 *   shell_output_hwm, to avoid matchtag exhaustion, etc. for chatty tasks.
 */
#define FLUX_SHELL_PLUGIN_NAME "output"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <unistd.h>
#include <fcntl.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"
#include "log.h"
#include "output/filehash.h"
#include "output/client.h"
#include "output/kvs.h"
#include "output/conf.h"
#include "output/output.h"
#include "output/task.h"


static int shell_output_data (struct shell_output *out, json_t *context)
{
    struct file_entry *fp;
    const char *stream = NULL;
    const char *rank = NULL;
    char *data = NULL;
    int len = 0;
    int rc = -1;

    if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
        shell_log_errno ("iodecode");
        return -1;
    }
    if (streq (stream, "stdout"))
        fp = out->stdout_fp;
    else
        fp = out->stderr_fp;

    if (file_entry_write (fp, rank, data, len) < 0)
        goto out;
    rc = 0;
out:
    free (data);
    return rc;
}

/*  Level prefix strings. Nominally, output log event 'level' integers
 *   are Internet RFC 5424 severity levels. In the context of flux-shell,
 *   the first 3 levels are equivalently "fatal" errors.
 */
static const char *levelstr[] = {
    "FATAL", "FATAL", "FATAL", "ERROR", " WARN", NULL, "DEBUG", "TRACE"
};

static void shell_output_log (struct shell_output *out, json_t *context)
{
    const char *msg = NULL;
    const char *file = NULL;
    const char *component = NULL;
    int rank = -1;
    int line = -1;
    int level = -1;
    int fd = out->stderr_fp->fd;
    json_error_t error;

    if (json_unpack_ex (context,
                        &error,
                        0,
                        "{ s?i s:i s:s s?s s?s s?i }",
                        "rank", &rank,
                        "level", &level,
                        "message", &msg,
                        "component", &component,
                        "file", &file,
                        "line", &line) < 0) {
        /*  Ignore log messages that cannot be unpacked so we don't
         *   log an error while logging.
         */
        return;
    }
    dprintf (fd, "flux-shell");
    if (rank >= 0)
        dprintf (fd, "[%d]", rank);
    if (level >= 0 && level <= FLUX_SHELL_TRACE)
        dprintf (fd, ": %s", levelstr [level]);
    if (component)
        dprintf (fd, ": %s", component);
    dprintf (fd, ": %s\n", msg);
}

static int shell_output_file (struct shell_output *out,
                              const char *name,
                              json_t *context)
{
    if (streq (name, "data")) {
        if (shell_output_data (out, context) < 0) {
            shell_log_errno ("shell_output_data");
            return -1;
        }
    }
    else if (streq (name, "log"))
        shell_output_log (out, context);
    return 0;
}

static void shell_output_decref (struct shell_output *out,
                                 flux_msg_handler_t *mh)
{
    if (--out->refcount == 0) {
        if (mh)
            flux_msg_handler_stop (mh);
        if (flux_shell_remove_completion_ref (out->shell, "output.write") < 0)
            shell_log_errno ("flux_shell_remove_completion_ref");

        /* no more output is coming, "close" kvs eventlog, check for
         * any output truncation, etc.
         */
        kvs_output_close (out->kvs);
    }
}

static void shell_output_decref_shell_rank (struct shell_output *out,
                                            int shell_rank,
                                            flux_msg_handler_t *mh)
{
    if (idset_test (out->active_shells, shell_rank)
        && idset_clear (out->active_shells, shell_rank) == 0)
        shell_output_decref (out, mh);
}

static int shell_output_write_leader (struct shell_output *out,
                                      const char *type,
                                      int shell_rank,
                                      json_t *o,
                                      flux_msg_handler_t *mh) // may be NULL
{
    struct output_stream *ostream = &out->conf->err;

    if (streq (type, "eof")) {
        shell_output_decref_shell_rank (out, shell_rank, mh);
        return 0;
    }
    if (streq (type, "data")) {
        const char *stream = "stderr"; // default to stderr
        (void) iodecode (o, &stream, NULL, NULL, NULL, NULL);
        if (streq (stream, "stdout"))
            ostream = &out->conf->out;
    }
    if (ostream->type == FLUX_OUTPUT_TYPE_KVS) {
        if (kvs_output_write_entry (out->kvs, type, o) < 0)
            shell_die_errno (1, "kvs_output_write");
    }
    else if (ostream->type == FLUX_OUTPUT_TYPE_FILE) {
        if (shell_output_file (out, type, o) < 0)
            shell_log_errno ("shell_output_file");
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
    int shell_rank;
    json_t *o;
    const char *type;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i s:o}",
                             "name", &type,
                             "shell_rank", &shell_rank,
                             "context", &o) < 0)
        goto error;
    if (shell_output_write_leader (out, type, shell_rank, o, mh) < 0)
        goto error;
    if (flux_respond (out->shell->h, msg, NULL) < 0)
        shell_log_errno ("flux_respond");
    return;
error:
    if (flux_respond_error (out->shell->h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond");
}

static int shell_output_handler (flux_plugin_t *p,
                                 const char *topic,
                                 flux_plugin_arg_t *args,
                                 void *arg)
{
    struct shell_output *out = arg;
    json_t *context;

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN, "o", &context) < 0) {
        shell_log_errno ("shell.output: flux_plugin_arg_unpack");
        return -1;
    }
    return task_output_list_write (out->task_outputs, context);
}

static void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;
        output_config_destroy (out->conf);
        output_client_destroy (out->client);
        task_output_list_destroy (out->task_outputs);
        filehash_destroy (out->files);
        kvs_output_destroy (out->kvs);
        idset_destroy (out->active_shells);
        free (out);
        errno = saved_errno;
    }
}

static int log_output (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct shell_output *out = data;
    int rc = 0;
    int level = -1;
    json_t *context = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "level", &level) < 0)
        return -1;
    if (level > FLUX_SHELL_NOTICE + out->shell->verbose)
        return 0;
    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN, "o", &context) < 0
        || shell_output_write_type (out, "log", context) < 0) {
        rc = -1;
    }
    return rc;
}

static int shell_lost (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct shell_output *out = data;
    int shell_rank;

    /*  A shell has been lost. We need to decref the output refcount by 1
     *  since we'll never hear from that shell to avoid rank 0 shell from
     *  hanging.
     */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "shell_rank", &shell_rank) < 0)
        return shell_log_errno ("shell.lost: unpack of shell_rank failed");
    shell_output_decref_shell_rank (out, shell_rank, NULL);
    shell_debug ("lost shell rank %d", shell_rank);
    return 0;
}

static int output_redirect_stream (struct shell_output *out,
                                   const char *name,
                                   struct output_stream *stream)
{
    if (stream->type == FLUX_OUTPUT_TYPE_FILE) {
        /*  Note: per-rank or per-task redirect events are not generated
         *  at this time. flux_shell_rank_mustache_render() with invalid
         *  rank will leave any task/node specific tags unexpanded in the
         *  posted path, e.g. flux-{{node.id}}-{{task.id}.out:
         */
        char *path;
        int shell_size = out->shell->info->shell_size;
        if (!(path = flux_shell_rank_mustache_render (out->shell,
                                                      shell_size,
                                                      stream->template))
            || kvs_output_redirect (out->kvs, name, path) < 0)
            shell_log_errno ("failed to post %s redirect event", name);
        free (path);
    }
    return 0;
}

static int shell_output_redirect (struct shell_output *out)
{
    if (output_redirect_stream (out, "stdout", &out->conf->out) < 0
        || output_redirect_stream (out, "stderr", &out->conf->err) < 0)
        return -1;
    return 0;
}

struct shell_output *shell_output_create (flux_shell_t *shell)
{
    struct shell_output *out;

    if (!(out = calloc (1, sizeof (*out))))
        return NULL;
    out->shell = shell;

    if (!(out->conf = output_config_create (shell)))
        goto error;

    if (!(out->files = filehash_create ()))
        goto error;

    if (!(out->task_outputs = task_output_list_create (out)))
        goto error;

    if (shell->info->shell_rank == 0) {
        int ntasks = out->shell->info->rankinfo.ntasks;
        if (flux_shell_service_register (shell,
                                         "write",
                                         shell_output_write_cb,
                                         out) < 0)
            goto error;

        /*  The shell.output.write service needs to wait for all
         *   remote shells and local tasks before the output destination
         *   can be closed. Therefore, set a reference counter for
         *   the number of remote shells (shell_size - 1), plus the
         *   number of tasks on the leader shell.
         *
         *  Remote shells and local tasks will cause the refcount
         *   to be decremented as they send EOF or exit.
         */
        out->refcount = (shell->info->shell_size - 1 + ntasks);

        /*  Account for active shells to avoid double-decrement of
         *  refcount when a shell exits prematurely
         */
        if (!(out->active_shells = idset_create (0, IDSET_FLAG_AUTOGROW))
            || idset_range_set (out->active_shells,
                                0,
                                shell->info->shell_size - 1) < 0)
            goto error;
        if (flux_shell_add_completion_ref (shell, "output.write") < 0)
            goto error;

        /* Create kvs output eventlog + header
         */
        if (!(out->kvs = kvs_output_create (shell)))
            goto error;

        /* If output is redirected to a file, post redirect event(s) to KVS
         */
        if (shell_output_redirect (out) < 0)
            goto error;

        /* Flush kvs output so eventlog is created
         */
        kvs_output_flush (out->kvs);
    }
    else if (!(out->client = output_client_create (shell))) {
        shell_log_errno ("failed to create output service client");
        goto error;
    }
    return out;
error:
    shell_output_destroy (out);
    return NULL;
}

static int shell_output_task_exit (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = flux_plugin_aux_get (p, "builtin.output");
    if (out->shell->info->shell_rank == 0)
        shell_output_decref (out, NULL);
    return 0;
}

static void shell_output_setup_file_entries (flux_plugin_t *p,
                                             struct shell_output *out)
{
    /* Set shell-wide stdout/stderr to go to the same place as the first
     * task. This is used for log information, and on rank 0 if there is
     * is only one output file for stdout and/or stderr.
     *
     * Note: these members are expected to be NULL if output is being
     * sent to the KVS for one or both streams.
     */
    out->stdout_fp = task_output_file_entry (out->task_outputs, "stdout", 0);
    out->stderr_fp = task_output_file_entry (out->task_outputs, "stderr", 0);
    if (out->stderr_fp) {
        shell_debug ("redirecting log messages to job output file");
        if (flux_plugin_add_handler (p, "shell.log", log_output, out) < 0)
            shell_log_errno ("failed to add shell.log handler");
        flux_shell_log_setlevel (FLUX_SHELL_QUIET, "eventlog");
    }
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

    shell_output_setup_file_entries (p, out);

    if (flux_plugin_aux_set (p,
                             "builtin.output",
                             out,
                             (flux_free_f) shell_output_destroy) < 0) {
        shell_output_destroy (out);
        return -1;
    }
    if (flux_plugin_add_handler (p,
                                 "shell.output",
                                 shell_output_handler,
                                 out) < 0) {
        shell_output_destroy (out);
        return -1;
    }

    if (flux_plugin_add_handler (p, "shell.lost", shell_lost, out) < 0)
        return shell_log_errno ("failed to add shell.log handler");

    return 0;
}

static int shell_output_reconnect (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = data;
    kvs_output_reconnect (out->kvs);
    return 0;
}

struct shell_builtin builtin_output = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .reconnect = shell_output_reconnect,
    .init = shell_output_init,
    .task_exit = shell_output_task_exit,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
