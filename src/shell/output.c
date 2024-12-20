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
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/eventlogger.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/parse_size.h"
#include "ccan/str/str.h"

#include "task.h"
#include "svc.h"
#include "internal.h"
#include "builtins.h"
#include "log.h"
#include "output/filehash.h"
#include "output/client.h"

#define SINGLEUSER_OUTPUT_LIMIT "1G"
#define MULTIUSER_OUTPUT_LIMIT  "10M"
#define OUTPUT_LIMIT_MAX        1073741824
/* 104857600 = 100M */
#define OUTPUT_LIMIT_WARNING    104857600

enum {
    FLUX_OUTPUT_TYPE_KVS = 1,
    FLUX_OUTPUT_TYPE_FILE = 2,
};

struct output_stream {
    int type;
    size_t bytes;
    const char *buffer_type;
    const char *template;
    const char *mode;
    int label;
    struct file_entry *fp;
};

struct shell_output {
    flux_shell_t *shell;
    struct output_client *client;
    const char *kvs_limit_string;
    size_t kvs_limit_bytes;
    struct eventlogger *ev;
    double batch_timeout;
    int refcount;
    struct idset *active_shells;
    json_t *output;
    struct filehash *files;
    struct output_stream stdout;
    struct output_stream stderr;
};

static int shell_output_redirect_stream (struct shell_output *out,
                                         flux_kvs_txn_t *txn,
                                         const char *stream,
                                         const char *path)
{
    struct idset *idset = NULL;
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
        shell_log_errno ("eventlog_entry_pack");
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
    idset_destroy (idset);
    errno = saved_errno;
    return rc;
}

static int shell_output_redirect (struct shell_output *out, flux_kvs_txn_t *txn)
{
    /* if file redirected, output redirect event */
    if (out->stdout.type == FLUX_OUTPUT_TYPE_FILE) {
        if (shell_output_redirect_stream (out,
                                          txn,
                                          "stdout",
                                          out->stdout.fp->path) < 0)
            return -1;
    }
    if (out->stderr.type == FLUX_OUTPUT_TYPE_FILE) {
        if (shell_output_redirect_stream (out,
                                          txn,
                                          "stderr",
                                          out->stderr.fp->path) < 0)
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

/*  Return true if entry is a kvs destination, false otherwise.
 *  If true, then then stream and len will be set to the stream and
 *   length of data in this entry.
 */
static bool entry_output_is_kvs (struct shell_output *out,
                                 json_t *entry,
                                 bool *stdoutp,
                                 int *lenp,
                                 bool *eofp)
{
    json_t *context;
    const char *name;
    const char *stream;

    if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
        shell_log_errno ("eventlog_entry_parse");
        return 0;
    }
    if (streq (name, "data")) {
        if (iodecode (context, &stream, NULL, NULL, lenp, eofp) < 0) {
            shell_log_errno ("iodecode");
            return 0;
        }
        if ((*stdoutp = streq (stream, "stdout")))
            return (out->stdout.type == FLUX_OUTPUT_TYPE_KVS);
        else
            return (out->stderr.type == FLUX_OUTPUT_TYPE_KVS);
    }
    return 0;
}

static bool check_kvs_output_limit (struct shell_output *out,
                                    bool is_stdout,
                                    int len)
{
    const char *stream;
    size_t *bytesp;
    size_t prev;

    if (is_stdout) {
        stream = "stdout";
        bytesp = &out->stdout.bytes;
    }
    else {
        stream = "stderr";
        bytesp = &out->stderr.bytes;
    }

    prev = *bytesp;
    *bytesp += len;

    if (*bytesp > out->kvs_limit_bytes) {
        /*  Only log an error when the threshold is reached.
        */
        if (prev <= out->kvs_limit_bytes)
            shell_warn ("%s will be truncated, %s limit exceeded",
                        stream,
                        out->kvs_limit_string);
        return true;
    }
    return false;
}

static int shell_output_kvs (struct shell_output *out)
{
    json_t *entry;
    size_t index;
    bool is_stdout;
    int len;
    bool eof;

    json_array_foreach (out->output, index, entry) {
        if (entry_output_is_kvs (out, entry, &is_stdout, &len, &eof)) {
            bool truncate = check_kvs_output_limit (out, is_stdout, len);
            if (!truncate || eof) {
                if (eventlogger_append_entry (out->ev, 0, "output", entry) < 0)
                    return shell_log_errno ("eventlogger_append");
            }
        }
    }
    return 0;
}

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

    if (output->type == FLUX_OUTPUT_TYPE_FILE) {
        if (file_entry_write (output->fp, rank, data, len) < 0)
        goto out;
    }
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
        if (streq (name, "data")) {
            if (shell_output_data (out, context) < 0) {
                shell_log_errno ("shell_output_data");
                return -1;
            }
        }
        else if (streq (name, "log"))
            shell_output_log (out, context);
   }
    return 0;
}

static void output_truncation_warning (struct shell_output *out)
{
    bool warned = false;
    if (out->stderr.type == FLUX_OUTPUT_TYPE_KVS
        && out->stderr.bytes > out->kvs_limit_bytes) {
        shell_warn ("stderr: %zu of %zu bytes truncated",
                    out->stderr.bytes - out->kvs_limit_bytes,
                    out->stderr.bytes);
        warned = true;
    }
    if (out->stdout.type == FLUX_OUTPUT_TYPE_KVS &&
        out->stdout.bytes > out->kvs_limit_bytes) {
        shell_warn ("stdout: %zu of %zu bytes truncated",
                    out->stdout.bytes - out->kvs_limit_bytes,
                    out->stdout.bytes);
        warned = true;
    }
    if (out->stderr.type == FLUX_OUTPUT_TYPE_KVS
        && (out->stderr.bytes > OUTPUT_LIMIT_WARNING
            && out->stderr.bytes <= OUTPUT_LIMIT_MAX)) {
        shell_warn ("high stderr volume (%s), "
                    "consider redirecting to a file next time "
                    "(e.g. use --output=FILE)",
                    encode_size (out->stderr.bytes));
        warned = true;
    }
    if (out->stdout.type == FLUX_OUTPUT_TYPE_KVS
        && (out->stdout.bytes > OUTPUT_LIMIT_WARNING
            && out->stdout.bytes <= OUTPUT_LIMIT_MAX)) {
        shell_warn ("high stdout volume (%s), "
                    "consider redirecting to a file next time "
                    "(e.g. use --output=FILE)",
                    encode_size (out->stdout.bytes));
        warned = true;
    }
    /* Ensure KVS output is flushed to eventlogger if a warning was issued:
     */
    if (warned)
        shell_output_kvs (out);
}

static void shell_output_decref (struct shell_output *out,
                                 flux_msg_handler_t *mh)
{
    if (--out->refcount == 0) {
        output_truncation_warning (out);
        if (mh)
            flux_msg_handler_stop (mh);
        if (flux_shell_remove_completion_ref (out->shell, "output.write") < 0)
            shell_log_errno ("flux_shell_remove_completion_ref");

        /* no more output is coming, flush the last batch of output */
        if ((out->stdout.type == FLUX_OUTPUT_TYPE_KVS
            || (out->stderr.type == FLUX_OUTPUT_TYPE_KVS))) {
            if (eventlogger_flush (out->ev) < 0)
                shell_log_errno ("eventlogger_flush");
        }
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
    json_t *entry;

    if (streq (type, "eof")) {
        shell_output_decref_shell_rank (out, shell_rank, mh);
        return 0;
    }
    if (!(entry = eventlog_entry_pack (0., type, "O", o))) // increfs 'o'
        goto error;
    if (json_array_append_new (out->output, entry) < 0) {
        json_decref (entry);
        errno = ENOMEM;
        goto error;
    }
    if ((out->stdout.type == FLUX_OUTPUT_TYPE_KVS
         || (out->stderr.type == FLUX_OUTPUT_TYPE_KVS))) {
        if (shell_output_kvs (out) < 0)
            shell_die_errno (1, "shell_output_kvs");
    }
    if ((out->stdout.type == FLUX_OUTPUT_TYPE_FILE
         || (out->stderr.type == FLUX_OUTPUT_TYPE_FILE))) {
        if (shell_output_file (out) < 0)
            shell_log_errno ("shell_output_file");
    }
    if (json_array_clear (out->output) < 0) {
        shell_log_error ("json_array_clear failed");
        goto error;
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

void shell_output_destroy (struct shell_output *out)
{
    if (out) {
        int saved_errno = errno;

        output_client_destroy (out->client);
        filehash_destroy (out->files);

        if (out->output && json_array_size (out->output) > 0) { // leader only
            if ((out->stdout.type == FLUX_OUTPUT_TYPE_KVS)
                || (out->stderr.type == FLUX_OUTPUT_TYPE_KVS)) {
                if (shell_output_kvs (out) < 0)
                    shell_log_errno ("shell_output_kvs");
            }
            if ((out->stdout.type == FLUX_OUTPUT_TYPE_FILE
                 || (out->stderr.type == FLUX_OUTPUT_TYPE_FILE))) {
                if (shell_output_file (out) < 0)
                    shell_log_errno ("shell_output_file");
            }
        }
        json_decref (out->output);
        eventlogger_destroy (out->ev);
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

static int shell_output_type_file_setup (struct shell_output *out)
{
    if (out->stdout.type == FLUX_OUTPUT_TYPE_FILE
        && !(out->stdout.fp = shell_output_open_file (out, &out->stdout)))
        return -1;

    if (out->stderr.type == FLUX_OUTPUT_TYPE_FILE
        && !(out->stderr.fp = shell_output_open_file (out, &out->stderr)))
        return -1;
    return 0;
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
    /* emit initial output events.
     */
    if (shell_output_kvs_init (out, o) < 0) {
        shell_log_errno ("shell_output_kvs_init");
        goto error;
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

static int output_eventlogger_reconnect (flux_plugin_t *p,
                                         const char *topic,
                                         flux_plugin_arg_t *args,
                                         void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);

    /* during a reconnect, response to event logging may not occur,
     * thus output_unref() may not be called.  Clear all completion
     * references to inflight transactions.
     */

    while (flux_shell_remove_completion_ref (shell, "output.txn") == 0);
    return 0;
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

static int get_output_limit (struct shell_output *out)
{
    json_t *val = NULL;
    uint64_t size;

    /*  For single-user instances, cap at reasonable size limit.
     *  O/w use the default multiuser output limit:
     */
    if (out->shell->broker_owner == getuid())
        out->kvs_limit_string = SINGLEUSER_OUTPUT_LIMIT;
    else
        out->kvs_limit_string = MULTIUSER_OUTPUT_LIMIT;

    if (flux_shell_getopt_unpack (out->shell,
                                  "output",
                                  "{s?o}",
                                  "limit", &val) < 0) {
        shell_log_error ("Unable to unpack shell output.limit");
        return -1;
    }
    if (val != NULL) {
        if (json_is_integer (val)) {
            json_int_t limit = json_integer_value (val);
            if (limit <= 0 || limit > OUTPUT_LIMIT_MAX) {
                shell_log ("Invalid KVS output.limit=%ld", (long) limit);
                return -1;
            }
            out->kvs_limit_bytes = (size_t) limit;
            /*  Need a string representation of limit for errors
             */
            char *s = strdup (encode_size (out->kvs_limit_bytes));
            if (s && flux_shell_aux_set (out->shell, NULL, s, free) < 0)
                free (s);
            else
                out->kvs_limit_string = s;
            return 0;
        }
        if (!(out->kvs_limit_string = json_string_value (val))) {
            shell_log_error ("Unable to convert output.limit to string");
            return -1;
        }
    }
    if (parse_size (out->kvs_limit_string, &size) < 0
        || size == 0
        || size > OUTPUT_LIMIT_MAX) {
        shell_log ("Invalid KVS output.limit=%s", out->kvs_limit_string);
        return -1;
    }
    out->kvs_limit_bytes = (size_t) size;
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

    if (get_output_limit (out) < 0)
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
        if (!(out->output = json_array ())) {
            errno = ENOMEM;
            goto error;
        }
        if (out->stdout.type == FLUX_OUTPUT_TYPE_FILE
            || out->stderr.type == FLUX_OUTPUT_TYPE_FILE) {
            if (!(out->files = filehash_create ())
                || shell_output_type_file_setup (out) < 0)
                goto error;
        }
        if (output_eventlogger_start (out) < 0)
            goto error;
        if (shell_output_header (out) < 0)
            goto error;
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

struct shell_builtin builtin_output = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .reconnect = output_eventlogger_reconnect,
    .init = shell_output_init,
    .task_init = shell_output_task_init,
    .task_exit = shell_output_task_exit,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
