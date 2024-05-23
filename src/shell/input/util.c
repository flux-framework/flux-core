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
#include "src/common/libeventlog/eventlog.h"

#include "util.h"
#include "internal.h"

static void input_put_kvs_completion (flux_future_t *f, void *arg)
{
    flux_shell_t *shell = arg;

    if (flux_future_get (f, NULL) < 0)
        /* failing to write stdin to input is a fatal error */
        shell_die (1, "input_service_put_kvs: %s", strerror (errno));
    flux_future_destroy (f);

    if (flux_shell_remove_completion_ref (shell, "input.kvs") < 0)
        shell_log_errno ("flux_shell_remove_completion_ref");
}

int input_eventlog_put (flux_shell_t *shell, json_t *context)
{
    flux_t *h;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *entry = NULL;
    char *entrystr = NULL;
    int saved_errno;
    int rc = -1;

    if (!(h = flux_shell_get_flux (shell)))
        goto error;
    if (!(entry = eventlog_entry_pack (0.0, "data", "O", context)))
        goto error;
    if (!(entrystr = eventlog_entry_encode (entry)))
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "input", entrystr) < 0)
        goto error;
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        goto error;
    if (flux_future_then (f, -1, input_put_kvs_completion, shell) < 0)
        goto error;
    if (flux_shell_add_completion_ref (shell, "input.kvs") < 0) {
        shell_log_errno ("flux_shell_remove_completion_ref");
        goto error;
    }
    /* f memory responsibility of input_service_put_kvs_completion()
     * callback */
    f = NULL;
    rc = 0;
 error:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    free (entrystr);
    json_decref (entry);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
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
