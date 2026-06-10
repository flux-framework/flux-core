/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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

#define FLUX_SHELL_PLUGIN_NAME "input.util"

#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/eventlogger.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "util.h"
#include "internal.h"

/* Set a low maximum input limit for now. Moderate amounts of input
 * written to the KVS cause issues in current Flux due to improper buffering
 * resulting in lots of KVS appends. When this is addressed, this limit could
 * be raised.
 */
#define INPUT_LIMIT_MAX   33554432 /* 32M */
#define INPUT_EVENT_DATA  "data"
#define INPUT_STATS_KEY   "input_stats"
#define DEFAULT_BATCH_TIMEOUT 0.5

struct input_stats {
    flux_shell_t *shell;
    size_t stdin_bytes;
    size_t limit_bytes;
    struct eventlogger *ev;
};

static void input_stats_destroy (struct input_stats *stats)
{
    if (stats) {
        int saved_errno = errno;
        if (stats->ev && eventlogger_flush (stats->ev) < 0)
            shell_log_errno ("eventlogger_flush");
        eventlogger_destroy (stats->ev);
        free (stats);
        errno = saved_errno;
    }
}

static void input_ref (struct eventlogger *ev, void *arg)
{
    struct input_stats *stats = arg;
    flux_shell_add_completion_ref (stats->shell, "input.txn");
}

static void input_unref (struct eventlogger *ev, void *arg)
{
    struct input_stats *stats = arg;
    flux_shell_remove_completion_ref (stats->shell, "input.txn");
}

static int get_input_limit (struct input_stats *stats)
{
    json_t *val = NULL;
    uint64_t size;
    const char *limit_string = "10M";

    if (flux_shell_getopt_unpack (stats->shell,
                                  "input",
                                  "{s?o}",
                                  "limit", &val) < 0) {
        shell_log_error ("Unable to unpack shell input.limit");
        return -1;
    }
    if (val != NULL) {
        if (json_is_integer (val)) {
            json_int_t limit = json_integer_value (val);
            if (limit <= 0 || limit > INPUT_LIMIT_MAX) {
                shell_log ("Invalid KVS input.limit=%ld", (long) limit);
                return -1;
            }
            stats->limit_bytes = (size_t) limit;
            return 0;
        }
        if (!(limit_string = json_string_value (val))) {
            shell_log_error ("Unable to convert input.limit to string");
            return -1;
        }
    }
    if (parse_size (limit_string, &size) < 0
        || size == 0
        || size > INPUT_LIMIT_MAX) {
        shell_log ("Invalid KVS input.limit=%s", limit_string);
        return -1;
    }
    stats->limit_bytes = (size_t) size;
    return 0;
}

static struct input_stats *get_input_stats (flux_shell_t *shell)
{
    struct input_stats *stats;

    stats = flux_shell_aux_get (shell, INPUT_STATS_KEY);
    if (!stats) {
        flux_t *h;
        double batch_timeout = DEFAULT_BATCH_TIMEOUT;
        struct eventlogger_ops ops = {
            .busy = input_ref,
            .idle = input_unref
        };

        if (!(stats = calloc (1, sizeof (*stats))))
            return NULL;
        stats->shell = shell;

        if (flux_shell_getopt_unpack (shell,
                                      "input",
                                      "{s?F}",
                                      "batch-timeout", &batch_timeout) < 0) {
            shell_log_errno ("invalid input.batch-timeout option");
            goto error;
        }

        shell_debug ("input batch timeout = %.3fs", batch_timeout);

        if (!(h = flux_shell_get_flux (shell)))
            goto error;

        if (!(stats->ev = eventlogger_create (h, batch_timeout, &ops, stats))) {
            shell_log_errno ("eventlogger_create");
            goto error;
        }

        if (get_input_limit (stats) < 0
            || flux_shell_aux_set (shell,
                                   INPUT_STATS_KEY,
                                   stats,
                                   (flux_free_f) input_stats_destroy) < 0) {
            goto error;
        }
    }
    return stats;
error:
    input_stats_destroy (stats);
    return NULL;
}


static void check_input_limit (flux_shell_t *shell, int len)
{
    struct input_stats *stats;

    if (!(stats = get_input_stats (shell)))
        shell_die_errno (1, "failed to get input stats");

    stats->stdin_bytes += len;

    if (stats->stdin_bytes > stats->limit_bytes) {
        shell_die (1,
                   "stdin exceeds %s limit. "
                   "Try file input (flux submit --input=FILE) "
                   "or redirect input to your command to avoid the KVS "
                   "for large amounts of input",
                   encode_size (stats->limit_bytes));
    }
}

int input_eventlog_put_event (flux_shell_t *shell,
                              const char *name,
                              json_t *context)
{
    struct input_stats *stats;
    int len = 0;

    if (streq (name, INPUT_EVENT_DATA)) {
        if (iodecode (context, NULL, NULL, NULL, &len, NULL) == 0)
            check_input_limit (shell, len);
    }

    if (!(stats = get_input_stats (shell)))
        return -1;

    return eventlogger_append_pack (stats->ev,
                                    0,
                                    "input",
                                    name,
                                    "O",
                                    context);
}

static int input_kvs_eventlog_init (flux_shell_t *shell, json_t *header)
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
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "input", headerstr) < 0)
        goto error;
    if (!(f = flux_kvs_commit (shell->h, NULL, 0, txn)))
        goto error;
    /* Synchronously wait for kvs commit to complete to ensure
     * guest.input exists before passing shell initialization barrier.
     * This is required because tasks will immediately try to watch
     * input eventlog on starting.
     */
    if (flux_future_get (f, NULL) < 0)
        shell_die_errno (1, "failed to create input eventlog");
    rc = 0;
 error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (headerstr);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

int input_eventlog_init (flux_shell_t *shell)
{
    json_t *o = NULL;
    int rc = -1;

    /*  Validate input.limit early so invalid values are caught
     *  at job initialization rather than when stdin is first written.
     */
    if (!get_input_stats (shell)) {
        shell_log_errno ("failed to initialize input stats");
        goto error;
    }

    if (!(o = eventlog_entry_pack (0,
                                   "header",
                                   "{s:i s:{s:s} s:{s:i} s:{}}",
                                   "version", 1,
                                   "encoding",
                                    "stdin", "UTF-8",
                                   "count",
                                    "stdin", 1,
                                   "options")))
        goto error;
    if (input_kvs_eventlog_init (shell, o) < 0) {
        shell_log_errno ("input_service_kvs_init");
        goto error;
    }
    rc = 0;
 error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
