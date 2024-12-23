/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* std output kvs writer
 *
 * Handle adherence to RFC 24 for KVS output:
 *  - write RFC 24 header event at shell initialization
 *  - uses the 'eventlogger' abstraction to support batched
 *    updates (default batch timeout of 0.5s can be overridden
 *    with the output.batch-timeout shell option).
 *  - shell completion reference is taken when eventlogger is
 *    "busy" and dropped when "idle".
 *  - A kvs output limit is supported with different limits for
 *    single vs multiuser instances (see SINGLEUSER_OUTPUT_LIMIT
 *    and MULTIUSER_OUTPUT_LIMIT below) Output is truncated once
 *    the limit is reached and a warning is logged.
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

/* Note: necessary for shell_log functions
 */
#define FLUX_SHELL_PLUGIN_NAME "output.kvs"

#include "src/common/libioencode/ioencode.h"
#include "src/common/libeventlog/eventlogger.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "internal.h"
#include "info.h"
#include "output/kvs.h"

#define DEFAULT_BATCH_TIMEOUT 0.5

#define SINGLEUSER_OUTPUT_LIMIT "1G"
#define MULTIUSER_OUTPUT_LIMIT  "10M"
#define OUTPUT_LIMIT_MAX        1073741824
/* 104857600 = 100M */
#define OUTPUT_LIMIT_WARNING    104857600

struct kvs_output {
    flux_shell_t *shell;
    int ntasks;
    const char *limit_string;
    size_t limit_bytes;
    size_t stdout_bytes;
    size_t stderr_bytes;
    struct eventlogger *ev;
};

static void kvs_output_truncation_warning (struct kvs_output *kvs)
{
    if (kvs->stderr_bytes > kvs->limit_bytes) {
        shell_warn ("stderr: %zu of %zu bytes truncated",
                    kvs->stderr_bytes - kvs->limit_bytes,
                    kvs->stderr_bytes);
    }
    if (kvs->stdout_bytes > kvs->limit_bytes) {
        shell_warn ("stdout: %zu of %zu bytes truncated",
                    kvs->stdout_bytes - kvs->limit_bytes,
                    kvs->stdout_bytes);
    }
    if (kvs->stderr_bytes > OUTPUT_LIMIT_WARNING
        && kvs->stderr_bytes <= OUTPUT_LIMIT_MAX) {
        shell_warn ("high stderr volume (%s), "
                    "consider redirecting to a file next time "
                    "(e.g. use --output=FILE)",
                    encode_size (kvs->stderr_bytes));
    }
    if (kvs->stdout_bytes > OUTPUT_LIMIT_WARNING
        && kvs->stdout_bytes <= OUTPUT_LIMIT_MAX) {
        shell_warn ("high stdout volume (%s), "
                    "consider redirecting to a file next time "
                    "(e.g. use --output=FILE)",
                    encode_size (kvs->stdout_bytes));
    }
}

void kvs_output_flush (struct kvs_output *kvs)
{
    if (eventlogger_flush (kvs->ev) < 0)
        shell_log_errno ("eventlogger_flush");
}

void kvs_output_close (struct kvs_output *kvs)
{
    if (kvs) {
        kvs_output_truncation_warning (kvs);
        kvs_output_flush (kvs);
    }
}

void kvs_output_destroy (struct kvs_output *kvs)
{
    if (kvs) {
        int saved_errno = errno;
        if (kvs->ev && eventlogger_flush (kvs->ev) < 0)
            shell_log_errno ("eventlogger_flush");
        eventlogger_destroy (kvs->ev);
        free (kvs);
        errno = saved_errno;
    }
}

static int get_output_limit (struct kvs_output *kvs)
{
    json_t *val = NULL;
    uint64_t size;

    /*  For single-user instances, cap at reasonable size limit.
     *  O/w use the default multiuser output limit:
     */
    if (kvs->shell->broker_owner == getuid())
        kvs->limit_string = SINGLEUSER_OUTPUT_LIMIT;
    else
        kvs->limit_string = MULTIUSER_OUTPUT_LIMIT;

    if (flux_shell_getopt_unpack (kvs->shell,
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
            kvs->limit_bytes = (size_t) limit;
            /*  Need a string representation of limit for errors
             */
            char *s = strdup (encode_size (kvs->limit_bytes));
            if (s && flux_shell_aux_set (kvs->shell, NULL, s, free) < 0)
                free (s);
            else
                kvs->limit_string = s;
            return 0;
        }
        if (!(kvs->limit_string = json_string_value (val))) {
            shell_log_error ("Unable to convert output.limit to string");
            return -1;
        }
    }
    if (parse_size (kvs->limit_string, &size) < 0
        || size == 0
        || size > OUTPUT_LIMIT_MAX) {
        shell_log ("Invalid KVS output.limit=%s", kvs->limit_string);
        return -1;
    }
    kvs->limit_bytes = (size_t) size;
    return 0;
}

static void output_ref (struct eventlogger *ev, void *arg)
{
    struct kvs_output *kvs = arg;
    flux_shell_add_completion_ref (kvs->shell, "output.txn");
}

static void output_unref (struct eventlogger *ev, void *arg)
{
    struct kvs_output *kvs = arg;
    flux_shell_remove_completion_ref (kvs->shell, "output.txn");
}

static int kvs_eventlogger_start (struct kvs_output *kvs,
                                  double batch_timeout)
{
    flux_t *h = flux_shell_get_flux (kvs->shell);
    struct eventlogger_ops ops = {
        .busy = output_ref,
        .idle = output_unref
    };

    shell_debug ("batch timeout = %.3fs", batch_timeout);

    kvs->ev = eventlogger_create (h, batch_timeout, &ops, kvs);
    if (!kvs->ev)
        return shell_log_errno ("eventlogger_create");
    return 0;
}

/* Write RFC 24 header event to KVS.  Assume:
 * - fixed utf-8 encoding for stdout, stderr
 * - no options
 * - no stdlog
 */
static int write_kvs_header (struct kvs_output *kvs)
{
    return eventlogger_append_pack (kvs->ev,
                                    0,
                                    "output",
                                    "header",
                                    "{s:i s:{s:s s:s} s:{s:i s:i} s:{}}",
                                    "version", 1,
                                    "encoding",
                                     "stdout", "UTF-8",
                                     "stderr", "UTF-8",
                                    "count",
                                     "stdout", kvs->ntasks,
                                     "stderr", kvs->ntasks,
                                    "options");
}

struct kvs_output *kvs_output_create (flux_shell_t *shell)
{
    struct kvs_output *kvs;
    double batch_timeout = DEFAULT_BATCH_TIMEOUT;

    if (flux_shell_getopt_unpack (shell,
                                  "output",
                                  "{s?F}",
                                  "batch-timeout", &batch_timeout) < 0) {
        shell_log_errno ("invalid output.batch-timeout option");
        return NULL;
    }

    if (!(kvs = calloc (1, sizeof (*kvs))))
        return NULL;

    kvs->shell = shell;
    kvs->ntasks = shell->info->total_ntasks;

    if (get_output_limit (kvs) < 0
        || kvs_eventlogger_start (kvs, batch_timeout) < 0
        || write_kvs_header (kvs) < 0)
        goto error;
    return kvs;
error:
    kvs_output_destroy (kvs);
    return NULL;
}

static char *encode_all_ranks (struct kvs_output *kvs)
{
    struct idset *ids;
    char *ranks = NULL;
    if (!(ids = idset_create (kvs->ntasks, 0))
        || idset_range_set (ids, 0, kvs->ntasks - 1) < 0
        || !(ranks = idset_encode (ids, IDSET_FLAG_RANGE|IDSET_FLAG_BRACKETS)))
        shell_log_errno ("failed to encode ranks idset");
    idset_destroy (ids);
    return ranks;
}

int kvs_output_redirect (struct kvs_output *kvs,
                         const char *stream,
                         const char *path)
{
    char *ranks = NULL;
    int rc = -1;

    if (!(ranks = encode_all_ranks (kvs)))
        return -1;
    if ((rc = eventlogger_append_pack (kvs->ev,
                                       0,
                                       "output",
                                       "redirect",
                                       "{s:s s:s s:s}",
                                       "stream", stream,
                                       "rank", ranks,
                                       "path", path) < 0))
        shell_log_errno ("eventlogger_append_pack");
    ERRNO_SAFE_WRAP (free, ranks);
    return rc;
}

static bool check_kvs_output_limit (struct kvs_output *kvs,
                                    const char *stream,
                                    int len)
{
    size_t prev;
    size_t *bytesp;

    if (streq (stream, "stdout"))
        bytesp = &kvs->stdout_bytes;
    else
        bytesp = &kvs->stderr_bytes;

    prev = *bytesp;
    *bytesp += len;

    if (*bytesp > kvs->limit_bytes) {
        /*  Only log an error when the threshold is reached.
         */
        if (prev <= kvs->limit_bytes)
            shell_warn ("%s will be truncated, %s limit exceeded",
                        stream,
                        kvs->limit_string);
        return true;
    }
    return false;
}

int kvs_output_write_entry (struct kvs_output *kvs,
                            const char *type,
                            json_t *context)
{
    int len = 0;
    bool eof;
    const char *stream = "stdout";
    bool truncate = false;

    if (streq (type, "data")
        && iodecode (context, &stream, NULL, NULL, &len, &eof) == 0) {
        truncate = check_kvs_output_limit (kvs, stream, len);
    }
    if (truncate && !eof)
        return 0;
    return eventlogger_append_pack (kvs->ev, 0, "output", type, "O", context);
}

void kvs_output_reconnect (struct kvs_output *kvs)
{
    /* during a reconnect, response to event logging may not occur,
     * thus output_unref() may not be called. Clear all completion
     * references to inflight transactions.
     */
    while (flux_shell_remove_completion_ref (kvs->shell, "output.txn") == 0);
}

/* vi: ts=4 sw=4 expandtab
 */
