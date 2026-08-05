/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* schedutil-shim.c - minimal libschedutil consumer for testing
 *
 * This is a bare-bones scheduler broker module whose sole purpose is to
 * exercise libschedutil (src/common/libschedutil).  It is NOT a real
 * scheduler: it tracks no resources and makes no scheduling decisions.
 * It completes the RFC 27 hello/ready handshake and, for each sched.alloc
 * request, drives one of libschedutil's response functions so a test can
 * observe what the library commits to the KVS and returns to job-manager.
 *
 * Behavior is controlled by two KVS keys that the test sets before
 * submitting jobs:
 *
 *   test.schedutil.mode  (string, default "success")
 *      "success" - respond with schedutil_alloc_respond_success_pack(),
 *                  granting the R stored in test.schedutil.R.
 *      "deny"    - respond with schedutil_alloc_respond_deny().
 *      "hold"    - cache the request without responding, so the test can
 *                  drive a sched.cancel (answered with
 *                  schedutil_alloc_respond_cancel()).
 *
 *   test.schedutil.R     (string) R granted in "success" mode.
 *
 * The most recently held alloc request message is retained so that a
 * cancel can be answered against it.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libschedutil/init.h"
#include "src/common/libschedutil/hello.h"
#include "src/common/libschedutil/ready.h"
#include "src/common/libschedutil/alloc.h"
#include "src/common/libschedutil/free.h"
#include "ccan/str/str.h"

struct shim {
    schedutil_t *util;
    const flux_msg_t *held;     // most recent held alloc request (mode=hold)
};

/* Read a string value from a KVS key, or NULL if unset.  Caller frees.
 */
static char *kvs_get_string (flux_t *h, const char *key)
{
    flux_future_t *f;
    const char *s;
    char *cpy = NULL;

    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        return NULL;
    if (flux_kvs_lookup_get (f, &s) == 0 && s)
        cpy = strdup (s);
    flux_future_destroy (f);
    return cpy;
}

/* Synchronously commit a string value to a KVS key.  Returns 0 on success,
 * -1 on error with errno set.  Caller frees nothing.
 */
static int kvs_put_string (flux_t *h, const char *key, const char *val)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    if (flux_kvs_txn_put (txn, 0, key, val) < 0
        || !(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return rc;
}

static void alloc_cb (flux_t *h, const flux_msg_t *msg, void *arg)
{
    struct shim *ctx = arg;
    char *mode = kvs_get_string (h, "test.schedutil.mode");
    char *R = NULL;

    if (mode && streq (mode, "deny")) {
        if (schedutil_alloc_respond_deny (ctx->util,
                                          msg,
                                          "denied for test") < 0)
            flux_log_error (h, "schedutil_alloc_respond_deny");
    }
    else if (mode && streq (mode, "hold")) {
        /* Annotate the pending request before caching it, so a test can
         * observe an annotation update on a job that has not been allocated.
         */
        if (schedutil_alloc_respond_annotate_pack (ctx->util,
                                                   msg,
                                                   "{s:{s:s}}",
                                                   "sched",
                                                   "reason_pending",
                                                   "held for test") < 0)
            flux_log_error (h, "schedutil_alloc_respond_annotate_pack");
        flux_msg_decref (ctx->held);
        ctx->held = flux_msg_incref (msg);
    }
    else { // "success" (default)
        if (!(R = kvs_get_string (h, "test.schedutil.R"))) {
            flux_log (h, LOG_ERR, "alloc: test.schedutil.R is unset");
            if (schedutil_alloc_respond_deny (ctx->util, msg, "no R set") < 0)
                flux_log_error (h, "schedutil_alloc_respond_deny");
        }
        /* Attach an annotation to exercise the success-response annotation
         * argument, mirroring how a real scheduler reports a resource summary.
         */
        else if (schedutil_alloc_respond_success_pack (ctx->util,
                                                       msg,
                                                       R,
                                                       "{s:{s:s}}",
                                                       "sched",
                                                       "resource_summary",
                                                       "rank[0-1]") < 0)
            flux_log_error (h, "schedutil_alloc_respond_success_pack");
    }
    free (mode);
    free (R);
}

static void cancel_cb (flux_t *h, const flux_msg_t *msg, void *arg)
{
    struct shim *ctx = arg;

    if (ctx->held) {
        if (schedutil_alloc_respond_cancel (ctx->util, ctx->held) < 0)
            flux_log_error (h, "schedutil_alloc_respond_cancel");
        flux_msg_decref (ctx->held);
        ctx->held = NULL;
    }
}

static void free_cb (flux_t *h, const flux_msg_t *msg, const char *R, void *arg)
{
    struct shim *ctx = arg;

    if (schedutil_free_respond (ctx->util, msg) < 0)
        flux_log_error (h, "schedutil_free_respond");
}

/* Record R for a job reported during the hello handshake so a test can
 * verify what libschedutil handed back -- in particular the partial R
 * computed when the job-manager reports resources released to housekeeping.
 * The R is stored at test.schedutil.hello.<jobid>.
 */
static int hello_cb (flux_t *h, const flux_msg_t *msg, const char *R, void *arg)
{
    flux_jobid_t id;
    char key[64];
    char *fail;

    if (flux_msg_unpack (msg, "{s:I}", "id", &id) < 0)
        return -1;
    /* Fail the callback on request so libschedutil raises a fatal exception
     * on the running job (the scheduler-cannot-reallocate path).
     */
    if ((fail = kvs_get_string (h, "test.schedutil.hello_fail"))) {
        int rc = streq (fail, "1") ? -1 : 0;
        free (fail);
        if (rc < 0)
            return -1;
    }
    (void)snprintf (key,
                    sizeof (key),
                    "test.schedutil.hello.%ju",
                    (uintmax_t)id);
    if (kvs_put_string (h, key, R) < 0) {
        flux_log_error (h, "hello_cb: recording R for %ju", (uintmax_t)id);
        return -1;
    }
    return 0;
}

static struct schedutil_ops ops = {
    .hello = hello_cb,
    .alloc = alloc_cb,
    .free = free_cb,
    .cancel = cancel_cb,
    .prioritize = NULL,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    struct shim ctx = {0};
    int rc = -1;
    const char *mode = "unlimited";
    int queue_depth = -1;

    /* Optional "ready-mode=MODE" argument selects the concurrency mode
     * passed to schedutil_ready() (default "unlimited"), letting a test
     * exercise the "limited=N" path.
     */
    for (int i = 0; i < argc; i++) {
        if (strstarts (argv[i], "ready-mode="))
            mode = argv[i] + 11;
        else {
            flux_log (h, LOG_ERR, "unknown module option: %s", argv[i]);
            errno = EINVAL;
            goto done;
        }
    }
    if (!(ctx.util = schedutil_create (h,
                                       SCHEDUTIL_HELLO_PARTIAL_OK,
                                       &ops,
                                       &ctx))) {
        flux_log_error (h, "schedutil_create");
        goto done;
    }
    /* Complete the hello/ready handshake before advertising running, so a
     * ready failure (e.g. an invalid mode) is a clean module load failure
     * rather than a runtime crash of an already-running module.
     */
    if (schedutil_hello (ctx.util) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto done;
    }
    if (schedutil_ready (ctx.util, mode, &queue_depth) < 0) {
        flux_log_error (h, "schedutil_ready");
        goto done;
    }
    flux_log (h, LOG_DEBUG, "ready: mode=%s queue_depth=%d", mode, queue_depth);
    if (flux_module_set_running (h) < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_decref (ctx.held);
    schedutil_destroy (ctx.util);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
