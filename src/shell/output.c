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

enum {
    FLUX_OUTPUT_TYPE_KVS = 1,
    FLUX_OUTPUT_TYPE_FILE = 2,
};

struct output_stream {
    int type;
    const char *buffer_type;
    const char *template;
    const char *mode;
    int label;
    struct file_entry *fp;
};

struct shell_output {
    flux_shell_t *shell;
    struct output_client *client;
    struct kvs_output *kvs;
    int refcount;
    struct idset *active_shells;
    struct filehash *files;
    struct output_stream stdout;
    struct output_stream stderr;
};


static int shell_output_data (struct shell_output *out, json_t *context)
{
    struct output_stream *output;
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
        output = &out->stdout;
    else
        output = &out->stderr;

    if (file_entry_write (output->fp, rank, data, len) < 0)
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
    int fd = out->stderr.fp->fd;
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
    struct output_stream *ostream = &out->stderr;

    if (streq (type, "eof")) {
        shell_output_decref_shell_rank (out, shell_rank, mh);
        return 0;
    }
    if (streq (type, "data")) {
        const char *stream = "stderr"; // default to stderr
        (void) iodecode (o, &stream, NULL, NULL, NULL, NULL);
        if (streq (stream, "stdout"))
            ostream = &out->stdout;
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

static int shell_output_write_type (struct shell_output *out,
                                    char *type,
                                    json_t *context)
{
    if (out->shell->info->shell_rank == 0) {
        if (shell_output_write_leader (out, type, 0, context, NULL) < 0)
            shell_log_errno ("shell_output_write_leader");
    }
    else if (output_client_send (out->client, type, context) < 0)
        shell_log_errno ("failed to send data to shell leader");
    return 0;
}

static int shell_output_write (struct shell_output *out,
                               int rank,
                               const char *stream,
                               const char *data,
                               int len,
                               bool eof)
{
    int rc;
    json_t *o = NULL;
    char rankstr[13];

    /* integer %d guaranteed to fit in 13 bytes
     */
    (void) snprintf (rankstr, sizeof (rankstr), "%d", rank);
    if (!(o = ioencode (stream, rankstr, data, len, eof))) {
        shell_log_errno ("ioencode");
        return -1;
    }
    rc = shell_output_write_type (out, "data", o);
    json_decref (o);
    return rc;
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
    return shell_output_write_type (out, "data", context);
}

static void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;
        output_client_destroy (out->client);
        filehash_destroy (out->files);
        kvs_output_destroy (out->kvs);
        idset_destroy (out->active_shells);
        free (out);
        errno = saved_errno;
    }
}

static struct file_entry *shell_output_open_file (struct shell_output *out,
                                                  struct output_stream *stream)
{
    char *path = NULL;
    int flags = O_CREAT | O_WRONLY;
    struct file_entry *fp = NULL;
    flux_error_t error;

    if (streq (stream->mode, "append"))
        flags |= O_APPEND;
    else if (streq (stream->mode, "truncate"))
        flags |= O_TRUNC;
    else
        shell_warn ("ignoring invalid output.mode=%s", stream->mode);

    if (stream->template == NULL) {
        shell_log_error ("path for file output not specified");
        return NULL;
    }

    if (!(path = flux_shell_mustache_render (out->shell, stream->template)))
        return NULL;

    if (!(fp = filehash_open (out->files, &error, path, flags, stream->label)))
        shell_log_error ("%s", error.text);
    free (path);
    return fp;
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

static int output_stream_getopts (flux_shell_t *shell,
                                  const char *name,
                                  struct output_stream *stream)
{
    const char *type = NULL;

    if (flux_shell_getopt_unpack (shell,
                                  "output",
                                  "{s?s s?{s?s s?s s?b s?{s?s}}}",
                                  "mode", &stream->mode,
                                  name,
                                   "type", &type,
                                   "path", &stream->template,
                                   "label", &stream->label,
                                   "buffer",
                                     "type", &stream->buffer_type) < 0) {
        shell_log_error ("failed to read %s output options", name);
        return -1;
    }
    if (type && streq (type, "kvs")) {
        stream->template = NULL;
        stream->type = FLUX_OUTPUT_TYPE_KVS;
        return 0;
    }
    if (stream->template)
        stream->type = FLUX_OUTPUT_TYPE_FILE;

    if (strcasecmp (stream->buffer_type, "none") == 0)
        stream->buffer_type = "none";
    else if (strcasecmp (stream->buffer_type, "line") == 0)
        stream->buffer_type = "line";
    else {
        shell_log_error ("invalid buffer type specified: %s",
                         stream->buffer_type);
        stream->buffer_type = "line";
    }
    return 0;
}

static int shell_output_open_files (struct shell_output *out)
{
    if (out->stdout.type == FLUX_OUTPUT_TYPE_FILE) {
        if (!(out->stdout.fp = shell_output_open_file (out, &out->stdout))
            || kvs_output_redirect (out->kvs,
                                    "stdout",
                                    out->stdout.fp->path) < 0)
            return -1;
    }
    if (out->stderr.type == FLUX_OUTPUT_TYPE_FILE) {
        if (!(out->stderr.fp = shell_output_open_file (out, &out->stderr))
            || kvs_output_redirect (out->kvs,
                                    "stderr",
                                    out->stderr.fp->path) < 0)
            return -1;
    }
    return 0;
}

struct shell_output *shell_output_create (flux_shell_t *shell)
{
    struct shell_output *out;

    if (!(out = calloc (1, sizeof (*out))))
        return NULL;
    out->shell = shell;

    out->stdout.type = FLUX_OUTPUT_TYPE_KVS;
    out->stdout.mode = "truncate";
    out->stdout.buffer_type = "line";
    if (output_stream_getopts (shell, "stdout", &out->stdout) < 0)
        goto error;

    /* stderr defaults except for buffer_type inherit from stdout:
     */
    out->stderr = out->stdout;
    out->stderr.buffer_type = "none";
    if (output_stream_getopts (shell, "stderr", &out->stderr) < 0)
        goto error;

    if (!(out->files = filehash_create ()))
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

        /* Open all output files if necessary
         */
        if (shell_output_open_files (out) < 0)
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

static int task_setup_buffering (struct shell_task *task,
                                 const char *stream,
                                 const char *buffer_type)
{
    /* libsubprocess defaults to line buffering, so we only need to
     * handle != line case */
    if (!strcasecmp (buffer_type, "none")) {
        char buf[64];
        snprintf (buf, sizeof (buf), "%s_LINE_BUFFER", stream);
        if (flux_cmd_setopt (task->cmd, buf, "false") < 0) {
            shell_log_errno ("flux_cmd_setopt");
            return -1;
        }
    }

    return 0;
}

static void task_line_output_cb (struct shell_task *task,
                                 const char *stream,
                                 void *arg)
{
    struct shell_output *out = arg;
    const char *data;
    int len;

    len = flux_subprocess_getline (task->proc, stream, &data);
    if (len < 0) {
        shell_log_errno ("read %s task %d", stream, task->rank);
    }
    else if (len > 0) {
        if (shell_output_write (out,
                                task->rank,
                                stream,
                                data,
                                len,
                                false) < 0)
            shell_log_errno ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_output_write (out,
                                task->rank,
                                stream,
                                NULL,
                                0,
                                true) < 0)
            shell_log_errno ("write eof %s task %d", stream, task->rank);
    }
}

static void task_none_output_cb (struct shell_task *task,
                                 const char *stream,
                                 void *arg)
{
    struct shell_output *out = arg;
    const char *data;
    int len;

    len = flux_subprocess_read_line (task->proc, stream, &data);
    if (len < 0) {
        shell_log_errno ("read line %s task %d", stream, task->rank);
    }
    else if (!len) {
        /* stderr is unbuffered */
        if ((len = flux_subprocess_read (task->proc, stream, &data)) < 0) {
            shell_log_errno ("read %s task %d", stream, task->rank);
            return;
        }
    }
    if (len > 0) {
        if (shell_output_write (out,
                                task->rank,
                                stream,
                                data,
                                len,
                                false) < 0)
            shell_log_errno ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_output_write (out,
                                task->rank,
                                stream,
                                NULL,
                                0,
                                true) < 0)
            shell_log_errno ("write eof %s task %d", stream, task->rank);
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
    void (*output_cb)(struct shell_task *, const char *, void *);

    if (!shell || !out || !(task = flux_shell_current_task (shell)))
        return -1;

    if (task_setup_buffering (task, "stdout", out->stdout.buffer_type) < 0)
        return -1;
    if (task_setup_buffering (task, "stderr", out->stderr.buffer_type) < 0)
        return -1;

    if (!strcasecmp (out->stdout.buffer_type, "line"))
        output_cb = task_line_output_cb;
    else
        output_cb = task_none_output_cb;
    if (flux_shell_task_channel_subscribe (task,
                                           "stdout",
                                           output_cb,
                                           out) < 0)
            return -1;
    if (!strcasecmp (out->stderr.buffer_type, "line"))
        output_cb = task_line_output_cb;
    else
        output_cb = task_none_output_cb;

    if (flux_shell_task_channel_subscribe (task,
                                           "stderr",
                                           output_cb,
                                           out) < 0)
        return -1;
    return 0;
}

static int shell_output_task_exit (flux_plugin_t *p,
                                   const char *topic,
                                   flux_plugin_arg_t *args,
                                   void *data)
{
    struct shell_output *out = flux_plugin_aux_get (p, "builtin.output");

    /*  Leader shell: decrement output.write refcount for each exiting
     *   task (in lieu of counting EOFs separately from stderr/out)
     */
    if (out->shell->info->shell_rank == 0)
        shell_output_decref (out, NULL);
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

    /*  If stderr is redirected to file, be sure to also copy log messages
     *   there as soon as file is opened
     */
    if (out->stderr.type == FLUX_OUTPUT_TYPE_FILE) {
        shell_debug ("redirecting log messages to job output file");
        if (flux_plugin_add_handler (p, "shell.log", log_output, out) < 0)
            return shell_log_errno ("failed to add shell.log handler");
        flux_shell_log_setlevel (FLUX_SHELL_QUIET, "eventlog");
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
    .task_init = shell_output_task_init,
    .task_exit = shell_output_task_exit,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
