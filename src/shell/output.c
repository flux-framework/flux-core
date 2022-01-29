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
 * If output goes to terminal, stdout/stderr is written to the KVS, or
 * stdout/stderr if written directly to a file, the leader shell
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
 * - In standalone mode, the loop:// connector enables RPCs to work
 * - In standalone mode, output is written to the shell's stdout/stderr not KVS
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
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/eventlogger.h"
#include "src/common/libioencode/ioencode.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"
#include "log.h"
#include "mustache.h"

enum {
    FLUX_OUTPUT_TYPE_TERM = 1,
    FLUX_OUTPUT_TYPE_KVS = 2,
    FLUX_OUTPUT_TYPE_FILE = 3,
};

struct shell_output_fd {
    int fd;
};

struct shell_output_type_file {
    struct shell_output_fd *fdp;
    char *path;
    int label;
};

struct shell_output {
    flux_shell_t *shell;
    struct eventlogger *ev;
    double batch_timeout;
    int refcount;
    int eof_pending;
    zlist_t *pending_writes;
    json_t *output;
    bool stopped;
    int stdout_type;
    int stderr_type;
    struct shell_output_type_file stdout_file;
    struct shell_output_type_file stderr_file;
    zhash_t *fds;
    const char *stdout_buffer_type;
    const char *stderr_buffer_type;
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
            shell_log_errno ("flux_subprocess_stream_stop %d:%s", task->rank, stream);
    }
    else {
        if (flux_subprocess_stream_start (task->proc, stream) < 0)
            shell_log_errno ("flux_subprocess_stream_start %d:%s", task->rank, stream);
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
            shell_log_errno ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "data")) {
            int output_type;
            FILE *f;
            const char *stream = NULL;
            const char *rank = NULL;
            char *data = NULL;
            int len = 0;
            if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
                shell_log_errno ("iodecode");
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
                fprintf (f, "%s: ", rank);
                fwrite (data, len, 1, f);
            }
            free (data);
        }
    }
    return 0;
}

static int shell_output_redirect_stream (struct shell_output *out,
                                         flux_kvs_txn_t *txn,
                                         const char *stream,
                                         const char *path)
{
    struct idset *idset;
    json_t *entry = NULL;
    char *entrystr = NULL;
    int saved_errno, rc = -1;
    char *rankptr = NULL;
    int ntasks = out->shell->info->rankinfo.ntasks;

    if (ntasks > 1) {
        int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;
        if (!(idset = idset_create (ntasks, 0))) {
            shell_log_errno ("idset_create");
            goto error;
        }
        if (idset_range_set (idset, 0, ntasks - 1) < 0) {
            shell_log_errno ("idset_range_set");
            goto error;
        }
        if (!(rankptr = idset_encode (idset, flags))) {
            shell_log_errno ("idset_encode");
            goto error;
        }
    }
    else {
        if (asprintf (&rankptr, "%d", 0) < 0) {
            shell_log_errno ("asprintf");
            goto error;
        }
    }

    if (!(entry = eventlog_entry_pack (0., "redirect",
                                       "{ s:s s:s s:s }",
                                       "stream", stream,
                                       "rank", rankptr,
                                       "path", path))) {
        shell_log_errno ("eventlog_entry_create");
        goto error;
    }
    if (!(entrystr = eventlog_entry_encode (entry))) {
        shell_log_errno ("eventlog_entry_encode");
        goto error;
    }
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "output", entrystr) < 0) {
        shell_log_errno ("flux_kvs_txn_put");
        goto error;
    }
    rc = 0;
error:
    /* on error, future destroyed via shell_output destroy */
    saved_errno = errno;
    json_decref (entry);
    free (entrystr);
    free (rankptr);
    errno = saved_errno;
    return rc;
}

static int shell_output_redirect (struct shell_output *out, flux_kvs_txn_t *txn)
{
    /* if file redirected, output redirect event */
    if (out->stdout_type == FLUX_OUTPUT_TYPE_FILE) {
        if (shell_output_redirect_stream (out,
                                          txn,
                                          "stdout",
                                          out->stdout_file.path) < 0)
            return -1;
    }
    if (out->stderr_type == FLUX_OUTPUT_TYPE_FILE) {
        if (shell_output_redirect_stream (out,
                                          txn,
                                          "stderr",
                                          out->stderr_file.path) < 0)
            return -1;
    }
    return 0;
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
    if (shell_output_redirect (out, txn) < 0)
        goto error;
    if (!(f = flux_kvs_commit (out->shell->h, NULL, 0, txn)))
        goto error;
    /* Wait synchronously for guest.output to be committed to kvs so
     * that the output eventlog is guaranteed to exist before shell.init
     * event is emitted to the exec.eventlog.
     */
    if (flux_future_get (f, NULL) < 0)
        shell_die_errno (1, "failed to create header in output eventlog");
    rc = 0;
error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (headerstr);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

static int entry_output_is_kvs (struct shell_output *out, json_t *entry)
{
    json_t *context;
    const char *name;
    const char *stream;
    if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
        shell_log_errno ("eventlog_entry_parse");
        return 0;
    }
    if (!strcmp (name, "data")) {
        if (iodecode (context, &stream, NULL, NULL, NULL, NULL) < 0) {
            shell_log_errno ("iodecode");
            return 0;
        }
        if (!strcmp (stream, "stdout"))
            return (out->stdout_type == FLUX_OUTPUT_TYPE_KVS);
        else
            return (out->stderr_type == FLUX_OUTPUT_TYPE_KVS);
    }
    return 0;
}

static int shell_output_kvs (struct shell_output *out)
{
    json_t *entry;
    size_t index;
    json_array_foreach (out->output, index, entry) {
        if (entry_output_is_kvs (out, entry) &&
            eventlogger_append_entry (out->ev, 0, "output", entry) < 0) {
            return shell_log_errno ("eventlogger_append");
        }
    }
    return 0;
}

static int shell_output_write_fd (int fd, const void *buf, size_t len)
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
            shell_log_errno ("eventlog_entry_parse");
            return -1;
        }
        if (!strcmp (name, "data")) {
            struct shell_output_type_file *ofp;
            int output_type;
            const char *stream = NULL;
            const char *rank = NULL;
            char *data = NULL;
            int len = 0;
            if (iodecode (context, &stream, &rank, &data, &len, NULL) < 0) {
                shell_log_errno ("iodecode");
                return -1;
            }
            if (!strcmp (stream, "stdout")) {
                output_type = out->stdout_type;
                ofp = &out->stdout_file;
            }
            else {
                output_type = out->stderr_type;
                ofp = &out->stderr_file;
            }
            if ((output_type == FLUX_OUTPUT_TYPE_FILE) && len > 0) {
                if (ofp->label) {
                    char *buf = NULL;
                    int buflen;
                    if ((buflen = asprintf (&buf, "%s: ", rank)) < 0)
                        return -1;
                    if (shell_output_write_fd (ofp->fdp->fd, buf, buflen) < 0) {
                        free (buf);
                        return -1;
                    }
                    free (buf);
                }
                if (shell_output_write_fd (ofp->fdp->fd, data, len) < 0)
                    return -1;
            }
            free (data);
        }
    }
    return 0;
}

static int shell_output_write_leader (struct shell_output *out,
                                      json_t *o,
                                      flux_msg_handler_t *mh) // may be NULL
{
    bool eof = false;
    json_t *entry;

    if (iodecode (o, NULL, NULL, NULL, NULL, &eof) < 0)
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
            shell_die_errno (1, "shell_output_term");
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS
         || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS))) {
        if (shell_output_kvs (out) < 0)
            shell_die_errno (1, "shell_output_kvs");
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_FILE
         || (out->stderr_type == FLUX_OUTPUT_TYPE_FILE))) {
        if (shell_output_file (out) < 0)
            shell_log_errno ("shell_output_file");
    }
    if (json_array_clear (out->output) < 0) {
        shell_log_error ("json_array_clear failed");
        goto error;
    }
    if (eof) {
        if (--out->eof_pending == 0) {
            flux_msg_handler_stop (mh);
            if (flux_shell_remove_completion_ref (out->shell, "output.write") < 0)
                shell_log_errno ("flux_shell_remove_completion_ref");
            /* no more output is coming, flush the last batch of
             * output */
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS
                 || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS))) {
                if (eventlogger_flush (out->ev) < 0)
                    shell_log_errno ("eventlogger_flush");
            }
        }
    }
    return 0;
error:
    return -1;
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
    json_t *o;

    if (flux_request_unpack (msg, NULL, "o", &o) < 0)
        goto error;
    if (shell_output_write_leader (out, o, mh) < 0)
        goto error;
    if (flux_respond (out->shell->h, msg, NULL) < 0)
        shell_log_errno ("flux_respond");
    return;
error:
    if (flux_respond_error (out->shell->h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond");
}

static void shell_output_write_completion (flux_future_t *f, void *arg)
{
    struct shell_output *out = arg;

    if (flux_future_get (f, NULL) < 0)
        shell_log_errno ("shell_output_write");
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
    char rankstr[64];

    snprintf (rankstr, sizeof (rankstr), "%d", rank);
    if (!(o = ioencode (stream, rankstr, data, len, eof))) {
        shell_log_errno ("ioencode");
        return -1;
    }

    if (out->shell->info->shell_rank == 0) {
        if (shell_output_write_leader (out, o, NULL) < 0)
            shell_log_errno ("shell_output_write_leader");
    }
    else {
        if (!(f = flux_shell_rpc_pack (out->shell, "write", 0, 0, "O", o)))
            goto error;
        if (flux_future_then (f, -1, shell_output_write_completion, out) < 0)
            goto error;
        if (zlist_append (out->pending_writes, f) < 0)
            shell_log_error ("zlist_append failed");
        if (zlist_size (out->pending_writes) >= shell_output_hwm)
            shell_output_control (out, true);
    }
    json_decref (o);

    return 0;

error:
    flux_future_destroy (f);
    json_decref (o);
    return -1;
}

static int shell_output_handler (flux_plugin_t *p,
                                 const char *topic,
                                 flux_plugin_arg_t *args,
                                 void *arg)
{
    struct shell_output *out = arg;
    int rank;
    const char *stream;
    const void *data;
    size_t len;

    if (flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s:s s:i s:s%}",
                                "stream", &stream,
                                "rank", &rank,
                                "data", &data, &len) < 0) {
        shell_log_errno ("shell.output: flux_plugin_arg_unpack");
        return -1;
    }
    return shell_output_write (out, rank, stream, data, len, len == 0);
}

static void shell_output_type_file_cleanup (struct shell_output_type_file *ofp)
{
    if (ofp->path)
        free (ofp->path);
}

void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;
        if (out->pending_writes) {
            flux_future_t *f;

            while ((f = zlist_pop (out->pending_writes))) { // follower only
                if (flux_future_get (f, NULL) < 0)
                    shell_log_errno ("shell_output_write");
                flux_future_destroy (f);
            }
            zlist_destroy (&out->pending_writes);
        }
        if (out->output && json_array_size (out->output) > 0) { // leader only
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_TERM)
                || (out->stderr_type == FLUX_OUTPUT_TYPE_TERM)) {
                if (shell_output_term (out) < 0)
                    shell_log_errno ("shell_output_term");
            }
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_KVS)
                || (out->stderr_type == FLUX_OUTPUT_TYPE_KVS)) {
                if (shell_output_kvs (out) < 0)
                    shell_log_errno ("shell_output_kvs");
            }
            if ((out->stdout_type == FLUX_OUTPUT_TYPE_FILE
                 || (out->stderr_type == FLUX_OUTPUT_TYPE_FILE))) {
                if (shell_output_file (out) < 0)
                    shell_log_errno ("shell_output_file");
            }
        }
        json_decref (out->output);
        shell_output_type_file_cleanup (&out->stdout_file);
        shell_output_type_file_cleanup (&out->stderr_file);
        if (out->fds) { // leader only
            struct shell_output_fd *fdp = zhash_first (out->fds);
            while (fdp) {
                close (fdp->fd);
                fdp = zhash_next (out->fds);
            }
            zhash_destroy (&out->fds);
        }
        eventlogger_destroy (out->ev);
        free (out);
        errno = saved_errno;
    }
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

static int shell_output_parse_type (struct shell_output *out,
                                    const char *typestr,
                                    int *typep)
{
    if (out->shell->standalone && !strcmp (typestr, "term"))
        (*typep) = FLUX_OUTPUT_TYPE_TERM;
    else if (!strcmp (typestr, "kvs"))
        (*typep) = FLUX_OUTPUT_TYPE_KVS;
    else if (!strcmp (typestr, "file"))
        (*typep) = FLUX_OUTPUT_TYPE_FILE;
    else
        return shell_log_errn (EINVAL,
                               "invalid output type specified '%s'",
                               typestr);
    return 0;
}

static int mustache_cb (FILE *fp, const char *name, void *arg)
{
    flux_shell_t *shell = arg;
    char value[128];

    /*  "jobid" is a synonym for "id" */
    if (strncmp (name, "jobid", 5) == 0)
        name += 3;
    if (strncmp (name, "id", 2) == 0) {
        const char *type = "f58";
        if (strlen (name) > 2) {
            if (name[2] != '.') {
                shell_log_error ("Unknown mustache tag '%s'", name);
                return -1;
            }
            type = name+3;
        }
        if (flux_job_id_encode (shell->info->jobid,
                                type,
                                value,
                                sizeof (value)) < 0) {
            if (errno == EPROTO)
                shell_log_error ("Invalid jobid encoding '%s' specified", name);
            else
                shell_log_errno ("flux_job_id_encode failed for %s", name);
            return -1;
        }
    }
    else {
        shell_log_error ("Unknown mustache tag '%s'", name);
        return -1;
    }
    return fputs (value, fp);
}

static char * shell_output_mustache_render (struct shell_output *out,
                                            const char *path)
{
    struct mustache_renderer *mr;
    char *result = NULL;

    mr = mustache_renderer_create (mustache_cb, out->shell);
    if (!mr) {
        shell_log_errno ("mustache_renderer_create");
        return NULL;
    }
    mustache_renderer_set_log (mr, shell_llog, NULL);
    result = mustache_render (mr, path);
    mustache_renderer_destroy (mr);
    return result;
}

static int
shell_output_setup_type_file (struct shell_output *out,
                              const char *stream,
                              struct shell_output_type_file *ofp,
                              struct shell_output_type_file *ofp_copy)
{
    const char *path = NULL;

    if (flux_shell_getopt_unpack (out->shell, "output",
                                  "{s:{s?:s}}",
                                  stream, "path", &path) < 0)
        return -1;

    if (path == NULL) {
        shell_log_error ("path for %s file output not specified", stream);
        return -1;
    }

    if (!(ofp->path = shell_output_mustache_render (out, path)))
        return -1;

    if (flux_shell_getopt_unpack (out->shell, "output",
                                  "{s:{s?:b}}",
                                  stream, "label", &(ofp->label)) < 0)
        return -1;

    if (ofp_copy) {
        if (!(ofp_copy->path = strdup (ofp->path)))
            return -1;
        ofp_copy->label = ofp->label;
    }

    return 0;
}

static int shell_output_setup_type (struct shell_output *out,
                                    const char *stream,
                                    int type,
                                    bool copy_to_stderr)
{
    if (type == FLUX_OUTPUT_TYPE_FILE) {
        struct shell_output_type_file *ofp = NULL;
        struct shell_output_type_file *ofp_copy = NULL;

        if (!strcmp (stream, "stdout"))
            ofp = &(out->stdout_file);
        else
            ofp = &(out->stderr_file);

        if (copy_to_stderr)
            ofp_copy = &(out->stderr_file);

        if (shell_output_setup_type_file (out, stream, ofp, ofp_copy) < 0)
            return -1;
    }
    return 0;
}

static int shell_output_check_alternate_output (struct shell_output *out)
{
    const char *stdout_typestr = NULL;
    const char *stderr_typestr = NULL;

    if (flux_shell_getopt_unpack (out->shell, "output",
                                  "{s?:{s?:s}}",
                                  "stdout", "type", &stdout_typestr) < 0)
        return -1;

    if (flux_shell_getopt_unpack (out->shell, "output",
                                  "{s?:{s?:s}}",
                                  "stderr", "type", &stderr_typestr) < 0)
        return -1;

    if (!stdout_typestr && !stderr_typestr)
        return 0;

    /* If stdout type is set but stderr type is not set, custom is for
     * stderr type to follow stdout type, including all settings */
    if (stdout_typestr && !stderr_typestr) {
        if (shell_output_parse_type (out,
                                     stdout_typestr,
                                     &(out->stdout_type)) < 0)
            return -1;

        out->stderr_type = out->stdout_type;

        if (shell_output_setup_type (out,
                                     "stdout",
                                     out->stdout_type,
                                     true) < 0)
            return -1;
    }
    else {
        if (stdout_typestr) {
            if (shell_output_parse_type (out,
                                         stdout_typestr,
                                         &(out->stdout_type)) < 0)
                return -1;
            if (shell_output_setup_type (out,
                                         "stdout",
                                         out->stdout_type,
                                         false) < 0)
                return -1;
        }
        if (stderr_typestr) {
            if (shell_output_parse_type (out,
                                         stderr_typestr,
                                         &(out->stderr_type)) < 0)
                return -1;
            if (shell_output_setup_type (out,
                                         "stderr",
                                         out->stderr_type,
                                         false) < 0)
                return -1;
        }
    }
    return 0;
}

static int parse_alternate_buffer_type (struct shell_output *out,
                                        const char *stream,
                                        const char **buffer_type_ptr)
{
    const char *buffer_type = NULL;

    if (flux_shell_getopt_unpack (out->shell, "output",
                                  "{s?:{s?:{s?:s}}}",
                                  stream,
                                  "buffer",
                                  "type", &buffer_type) < 0)
        return -1;

    if (buffer_type) {
        if (strcasecmp (buffer_type, "none")
            && strcasecmp (buffer_type, "line"))
            shell_log_error ("invalid buffer type specified: %s", buffer_type);
        else
            (*buffer_type_ptr) = buffer_type;
    }

    return 0;
}

static int shell_output_check_alternate_buffer_type (struct shell_output *out)
{
    if (parse_alternate_buffer_type (out,
                                     "stdout",
                                     &out->stdout_buffer_type) < 0)
        return -1;
    if (parse_alternate_buffer_type (out,
                                     "stderr",
                                     &out->stderr_buffer_type) < 0)
        return -1;
    return 0;
}

static struct shell_output_fd *shell_output_fd_create (int fd)
{
    struct shell_output_fd *fdp = calloc (1, sizeof (*fdp));
    if (!fdp)
        return NULL;
    fdp->fd = fd;
    return fdp;
}

static void shell_output_fd_destroy (void *data)
{
    struct shell_output_fd *fdp = data;
    if (fdp) {
        close (fdp->fd);
        free (fdp);
    }
}

static int shell_output_type_file_setup (struct shell_output *out,
                                         struct shell_output_type_file *ofp)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int open_flags = O_CREAT | O_TRUNC | O_WRONLY;
    struct shell_output_fd *fdp = NULL;
    int saved_errno, fd = -1;

    /* check if we're outputting to the same file as another stream */
    if ((fdp = zhash_lookup (out->fds, ofp->path))) {
        ofp->fdp = fdp;
        return 0;
    }

    if ((fd = open (ofp->path, open_flags, mode)) < 0) {
        shell_log_errno ("error opening output file '%s'", ofp->path);
        goto error;
    }

    if (!(fdp = shell_output_fd_create (fd)))
        goto error;

    if (zhash_insert (out->fds, ofp->path, fdp) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zhash_freefn (out->fds, ofp->path, shell_output_fd_destroy);
    ofp->fdp = fdp;
    return 0;

error:
    saved_errno = errno;
    shell_output_fd_destroy (fdp);
    errno = saved_errno;
    return -1;
}

/* Write RFC 24 header event to KVS.  Assume:
 * - fixed utf-8 encoding for stdout, stderr
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
                               "stdout", "UTF-8",
                               "stderr", "UTF-8",
                             "count",
                               "stdout", out->shell->info->total_ntasks,
                               "stderr", out->shell->info->total_ntasks,
                             "options");
    if (!o) {
        errno = ENOMEM;
        goto error;
    }
    if ((out->stdout_type == FLUX_OUTPUT_TYPE_TERM)
        || (out->stderr_type == FLUX_OUTPUT_TYPE_TERM)) {
        if (shell_output_term_init (out, o) < 0)
            shell_log_errno ("shell_output_term_init");
    }
    /* emit initial output events.
     * Call this as long as we're not standalone.
     */
    if (!out->shell->standalone) {
        if (shell_output_kvs_init (out, o) < 0) {
            shell_log_errno ("shell_output_kvs_init");
            goto error;
        }
    }
    rc = 0;
error:
    json_decref (o);
    return rc;
}

static void output_ref (struct eventlogger *ev, void *arg)
{
    struct shell_output *out = arg;
    flux_shell_add_completion_ref (out->shell, "output.txn");
}


static void output_unref (struct eventlogger *ev, void *arg)
{
    struct shell_output *out = arg;
    flux_shell_remove_completion_ref (out->shell, "output.txn");
}

static int output_eventlogger_start (struct shell_output *out)
{
    flux_t *h = flux_shell_get_flux (out->shell);
    struct eventlogger_ops ops = {
        .busy = output_ref,
        .idle = output_unref
    };

    out->batch_timeout = 0.5;

    if (flux_shell_getopt_unpack (out->shell,
                                  "output",
                                  "{s?F}",
                                  "batch-timeout", &out->batch_timeout) < 0)
        return shell_log_errno ("invalid output.batch-timeout option");

    shell_debug ("batch timeout = %.3fs", out->batch_timeout);

    out->ev = eventlogger_create (h, out->batch_timeout, &ops, out);
    if (!out->ev)
        return shell_log_errno ("eventlogger_create");
    return 0;
}

struct shell_output *shell_output_create (flux_shell_t *shell)
{
    struct shell_output *out;

    if (!(out = calloc (1, sizeof (*out))))
        return NULL;
    out->shell = shell;
    if (out->shell->standalone) {
        out->stdout_type = FLUX_OUTPUT_TYPE_TERM;
        out->stderr_type = FLUX_OUTPUT_TYPE_TERM;
    }
    else {
        out->stdout_type = FLUX_OUTPUT_TYPE_KVS;
        out->stderr_type = FLUX_OUTPUT_TYPE_KVS;
    }
    out->stdout_buffer_type = "line";
    out->stderr_buffer_type = "none";

    if (shell_output_check_alternate_output (out) < 0)
        goto error;
    if (shell_output_check_alternate_buffer_type (out) < 0)
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
                out->eof_pending += shell->info->total_ntasks;
            if (output_type_requires_service (out->stderr_type))
                out->eof_pending += shell->info->total_ntasks;
            if (flux_shell_add_completion_ref (shell, "output.write") < 0)
                goto error;
            if (!(out->output = json_array ())) {
                errno = ENOMEM;
                goto error;
            }
        }
        if (out->stdout_type == FLUX_OUTPUT_TYPE_FILE
            || out->stderr_type == FLUX_OUTPUT_TYPE_FILE) {
            if (!(out->fds = zhash_new ())) {
                errno = ENOMEM;
                goto error;
            }
            if (out->stdout_type == FLUX_OUTPUT_TYPE_FILE) {
                if (shell_output_type_file_setup (out, &(out->stdout_file)) < 0)
                    goto error;
            }
            if (out->stderr_type == FLUX_OUTPUT_TYPE_FILE) {
                if (shell_output_type_file_setup (out, &(out->stderr_file)) < 0)
                    goto error;
            }
        }
        if (output_eventlogger_start (out) < 0)
            goto error;
        if (shell_output_header (out) < 0)
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

    data = flux_subprocess_getline (task->proc, stream, &len);
    if (!data) {
        shell_log_errno ("read %s task %d", stream, task->rank);
    }
    else if (len > 0) {
        if (shell_output_write (out, task->rank, stream, data, len, false) < 0)
            shell_log_errno ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_output_write (out, task->rank, stream, NULL, 0, true) < 0)
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

    data = flux_subprocess_read_line (task->proc, stream, &len);
    if (!data) {
        shell_log_errno ("read line %s task %d", stream, task->rank);
    }
    else if (!len) {
        /* stderr is unbuffered */
        if (!(data = flux_subprocess_read (task->proc, stream, -1, &len))) {
            shell_log_errno ("read %s task %d", stream, task->rank);
            return;
        }
    }
    if (len > 0) {
        if (shell_output_write (out, task->rank, stream, data, len, false) < 0)
            shell_log_errno ("write %s task %d", stream, task->rank);
    }
    else if (flux_subprocess_read_stream_closed (task->proc, stream)) {
        if (shell_output_write (out, task->rank, stream, NULL, 0, true) < 0)
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

    if (task_setup_buffering (task, "stdout", out->stdout_buffer_type) < 0)
        return -1;
    if (task_setup_buffering (task, "stderr", out->stderr_buffer_type) < 0)
        return -1;

    if (output_type_requires_service (out->stdout_type)) {
        if (!strcasecmp (out->stdout_buffer_type, "line"))
            output_cb = task_line_output_cb;
        else
            output_cb = task_none_output_cb;
        if (flux_shell_task_channel_subscribe (task, "stdout",
                                               output_cb, out) < 0)
            return -1;
    }
    if (output_type_requires_service (out->stderr_type)) {
        if (!strcasecmp (out->stderr_buffer_type, "line"))
            output_cb = task_line_output_cb;
        else
            output_cb = task_none_output_cb;
        if (flux_shell_task_channel_subscribe (task, "stderr",
                                               output_cb, out) < 0)
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
    if (flux_plugin_add_handler (p,
                                 "shell.output",
                                 shell_output_handler,
                                 out) < 0) {
        shell_output_destroy (out);
        return -1;
    }
    return 0;
}

struct shell_builtin builtin_output = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = shell_output_init,
    .task_init = shell_output_task_init
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
