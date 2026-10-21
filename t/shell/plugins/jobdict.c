/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobdict.c - exercise the builtin jobdict plugin from an external plugin
 *
 * This drives the jobdict through the plugin stack exactly as a process
 * manager plugin would, using only the public flux/shell.h interface.
 *
 * In shell.init, each shell creates a jobdict and puts a global key and a
 * local key.  In shell.post-init (after all shells have registered their
 * services), each shell enters an exchange over all shells.  When the
 * exchange exits, each shell checks that every shell's global key is visible
 * and that local keys from other shells are not.  Shell 0 then enters a
 * subset exchange over just itself with a new key, and checks that the key is
 * visible only to it until a later full exchange makes it visible everywhere.
 * Invalid requests (duplicate create, unknown dictionary, exchanges that
 * exclude the caller or name a rank beyond the shell count) must fail.
 *
 * The jobdict-test-mode shell option selects an alternate scenario:
 *   chain-error  shell 0 never creates the dictionary, so the exchange
 *                fails on shell 1 and the failure cascades to its children
 *   eproto       shells 1-N enter tag T over all shells before shell 0
 *                enters it over 0-1, so shell 0's enter finds more queued
 *                child requests than its tree allows and fails
 *   concurrent   two exchanges are entered back to back with a re-put in
 *                between, so each must carry its own snapshot and the
 *                re-put value must survive to a later exchange
 *
 * Results are logged with shell_log()/shell_warn() for the test to grep,
 * and a failed check raises a job exception.
 */
#define FLUX_SHELL_PLUGIN_NAME "jobdict-test"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "ccan/str/str.h"

#define DICT_NAME "jobdict-test"
#define EXIT_TOPIC "jobdict-test.exchange-exit"

struct test_ctx {
    flux_shell_t *shell;
    int rank;
    int size;
    int phase;
    const char *mode;   // jobdict-test-mode shell option (default: NULL)
};

static void check_fail (struct test_ctx *ctx, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    shell_log_error ("%s", buf);
    flux_shell_raise ("exec", 0, "jobdict-test: %s", buf);
}

/* Call a jobdict topic with packed IN args.  Returns the args (caller
 * destroys) on success, or NULL after logging the jobdict errmsg.
 */
static flux_plugin_arg_t *call (struct test_ctx *ctx,
                                const char *topic,
                                const char *fmt,
                                ...)
{
    flux_plugin_arg_t *args;
    va_list ap;
    int rc;
    const char *errmsg = "failed";

    if (!(args = flux_plugin_arg_create ()))
        return NULL;
    va_start (ap, fmt);
    rc = flux_plugin_arg_vpack (args, FLUX_PLUGIN_ARG_IN, fmt, ap);
    va_end (ap);
    if (rc < 0)
        goto error;
    if (flux_shell_plugstack_call (ctx->shell, topic, args) < 0) {
        (void)flux_plugin_arg_unpack (args,
                                      FLUX_PLUGIN_ARG_OUT,
                                      "{s:s}",
                                      "errmsg", &errmsg);
        shell_log ("%s: %s", topic, errmsg);
        goto error;
    }
    return args;
error:
    flux_plugin_arg_destroy (args);
    return NULL;
}

static int put (struct test_ctx *ctx,
                const char *key,
                const char *val,
                int local)
{
    flux_plugin_arg_t *args;

    if (!(args = call (ctx,
                       "jobdict.put",
                       "{s:s s:s s:s s:b}",
                       "name", DICT_NAME,
                       "key", key,
                       "value", val,
                       "local", local)))
        return -1;
    flux_plugin_arg_destroy (args);
    return 0;
}

/* Returns 1 if found (and *valp set to a copy, caller frees), 0 if not
 * found, -1 on error.
 */
static int get_from (struct test_ctx *ctx,
                     const char *name,
                     const char *key,
                     char **valp)
{
    flux_plugin_arg_t *args;
    const char *val;
    int rc = -1;

    if (!(args = call (ctx,
                       "jobdict.get",
                       "{s:s s:s}",
                       "name", name,
                       "key", key)))
        return -1;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "value", &val) < 0) {
        rc = 0;
        goto out;
    }
    if (valp && !(*valp = strdup (val)))
        goto out;
    rc = 1;
out:
    flux_plugin_arg_destroy (args);
    return rc;
}

static int get (struct test_ctx *ctx, const char *key, char **valp)
{
    return get_from (ctx, DICT_NAME, key, valp);
}

static int exchange (struct test_ctx *ctx,
                    const char *tag,
                    const char *participants)
{
    flux_plugin_arg_t *args;

    if (!(args = call (ctx,
                       "jobdict.exchange",
                       "{s:s s:s s:s? s:s}",
                       "name", DICT_NAME,
                       "tag", tag,
                       "participants", participants,
                       "exit_topic", EXIT_TOPIC)))
        return -1;
    flux_plugin_arg_destroy (args);
    return 0;
}

/* Check that key 'key' has value 'expect' (or is absent if expect is NULL).
 */
static int expect_key (struct test_ctx *ctx,
                       const char *key,
                       const char *expect)
{
    char *val = NULL;
    int rc = get (ctx, key, &val);

    if (rc < 0) {
        check_fail (ctx, "get %s failed", key);
        return -1;
    }
    if (!expect && rc == 1) {
        check_fail (ctx, "%s=%s but expected absent", key, val);
        free (val);
        return -1;
    }
    if (expect && (rc == 0 || !streq (val, expect))) {
        check_fail (ctx, "%s=%s but expected %s", key, val ? val : "(absent)",
                    expect);
        free (val);
        return -1;
    }
    free (val);
    return 0;
}

static int check_all_global (struct test_ctx *ctx)
{
    char key[64];
    char val[64];

    for (int i = 0; i < ctx->size; i++) {
        snprintf (key, sizeof (key), "global.%d", i);
        snprintf (val, sizeof (val), "from-%d", i);
        if (expect_key (ctx, key, val) < 0)
            return -1;
        snprintf (key, sizeof (key), "local.%d", i);
        if (expect_key (ctx, key, i == ctx->rank ? val : NULL) < 0)
            return -1;
    }
    return 0;
}

/* Check that 'result' holds key cc.N=expect for every shell N.
 */
static int check_cc_result (struct test_ctx *ctx,
                            const char *tag,
                            json_t *result,
                            const char *expect)
{
    char key[64];

    for (int i = 0; i < ctx->size; i++) {
        const char *val;

        snprintf (key, sizeof (key), "cc.%d", i);
        if (!(val = json_string_value (json_object_get (result, key)))
            || !streq (val, expect)) {
            check_fail (ctx,
                        "%s result %s=%s but expected %s",
                        tag,
                        key,
                        val ? val : "(absent)",
                        expect);
            return -1;
        }
    }
    return 0;
}

static int put_cc (struct test_ctx *ctx, const char *val)
{
    char key[64];

    snprintf (key, sizeof (key), "cc.%d", ctx->rank);
    if (put (ctx, key, val, 0) < 0) {
        check_fail (ctx, "put %s=%s failed", key, val);
        return -1;
    }
    return 0;
}

static int test_done (struct test_ctx *ctx)
{
    if (flux_shell_remove_completion_ref (ctx->shell, "jobdict-test") < 0) {
        check_fail (ctx, "flux_shell_remove_completion_ref failed");
        return -1;
    }
    return 0;
}

/* Exit callback for the error scenarios: every exchange that reaches this
 * callback is expected to fail.
 */
static int exchange_exit_expect_error (struct test_ctx *ctx,
                                       const char *tag,
                                       const char *errmsg)
{
    if (!errmsg) {
        check_fail (ctx, "exchange %s unexpectedly succeeded", tag);
        return -1;
    }
    shell_log ("exchange %s failed as expected: %s", tag, errmsg);
    return test_done (ctx);
}

static int exchange_exit_concurrent (struct test_ctx *ctx,
                                     const char *tag,
                                     json_t *result)
{
    if (streq (tag, "A"))
        return check_cc_result (ctx, tag, result, "v1");
    if (streq (tag, "B")) {
        if (check_cc_result (ctx, tag, result, "v2") < 0)
            return -1;
        /* Re-put while C is in flight; D must carry the new value. */
        if (put_cc (ctx, "v3") < 0
            || exchange (ctx, "C", NULL) < 0
            || put_cc (ctx, "v4") < 0)
            return -1;
        return 0;
    }
    if (streq (tag, "C")) {
        if (check_cc_result (ctx, tag, result, "v3") < 0)
            return -1;
        return exchange (ctx, "D", NULL);
    }
    if (streq (tag, "D")) {
        char key[64];

        if (check_cc_result (ctx, tag, result, "v4") < 0)
            return -1;
        snprintf (key, sizeof (key), "cc.%d", ctx->rank);
        if (expect_key (ctx, key, "v4") < 0)
            return -1;
        shell_log ("concurrent checks passed");
        return test_done (ctx);
    }
    check_fail (ctx, "unexpected exchange tag %s", tag);
    return -1;
}

static int exchange_exit (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    struct test_ctx *ctx = data;
    const char *name;
    const char *tag;
    const char *errmsg = NULL;
    json_t *result = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s s?s s?o}",
                                "name", &name,
                                "tag", &tag,
                                "errmsg", &errmsg,
                                "result", &result) < 0) {
        check_fail (ctx, "error unpacking exchange exit args");
        return -1;
    }
    if (ctx->mode && streq (ctx->mode, "chain-error"))
        return exchange_exit_expect_error (ctx, tag, errmsg);
    if (ctx->mode && streq (ctx->mode, "eproto")) {
        if (streq (tag, "sync")) {
            if (errmsg) {
                check_fail (ctx, "exchange sync failed: %s", errmsg);
                return -1;
            }
            /* Shell 0 enters T late, expecting one child but with two
             * requests already queued, so the enter itself must fail.
             */
            if (ctx->rank == 0) {
                if (exchange (ctx, "T", "0-1") == 0) {
                    check_fail (ctx, "exchange T enter unexpectedly succeeded");
                    return -1;
                }
                shell_log ("exchange T enter failed as expected");
                return test_done (ctx);
            }
            return 0;
        }
        return exchange_exit_expect_error (ctx, tag, errmsg);
    }
    if (errmsg) {
        check_fail (ctx, "exchange %s failed: %s", tag, errmsg);
        return -1;
    }
    if (ctx->mode && streq (ctx->mode, "concurrent"))
        return exchange_exit_concurrent (ctx, tag, result);
    shell_log ("exchange %s exit: %zu keys in result",
               tag,
               json_object_size (result));

    if (streq (tag, "full1")) {
        if (check_all_global (ctx) < 0)
            return -1;
        shell_log ("full1 checks passed");
        /* Shell 0 alone exchanges a key with itself, then everyone enters
         * a second full exchange.  Shell 0 waits for the subset exchange to
         * complete before entering full2 so the ordering is deterministic.
         */
        if (ctx->rank == 0) {
            if (put (ctx, "subset.0", "subset-value", 0) < 0
                || exchange (ctx, "subset", "0") < 0)
                return -1;
        }
        else if (exchange (ctx, "full2", NULL) < 0)
            return -1;
    }
    else if (streq (tag, "subset")) {
        /* Only shell 0 participates and the key was put on shell 0 */
        if (expect_key (ctx, "subset.0", "subset-value") < 0)
            return -1;
        if (json_object_size (result) != 1) {
            check_fail (ctx, "subset result has %zu keys, expected 1",
                        json_object_size (result));
            return -1;
        }
        shell_log ("subset checks passed");
        if (exchange (ctx, "full2", NULL) < 0)
            return -1;
    }
    else if (streq (tag, "full2")) {
        /* The subset key must now have propagated everywhere */
        if (expect_key (ctx, "subset.0", "subset-value") < 0)
            return -1;
        shell_log ("full2 checks passed");
        return test_done (ctx);
    }
    return 0;
}

static int test_init (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    struct test_ctx *ctx = data;
    flux_plugin_arg_t *out;
    char key[64];
    char val[64];
    int k = 0;

    if (flux_shell_info_unpack (ctx->shell,
                                "{s:i s:i}",
                                "rank", &ctx->rank,
                                "size", &ctx->size) < 0) {
        check_fail (ctx, "flux_shell_info_unpack failed");
        return -1;
    }
    (void)flux_shell_getopt_unpack (ctx->shell, "jobdict-test-k", "i", &k);
    (void)flux_shell_getopt_unpack (ctx->shell,
                                    "jobdict-test-mode",
                                    "s",
                                    &ctx->mode);

    /* In the chain-error scenario shell 0 never creates the dictionary,
     * so requests sent to it fail.
     */
    if (ctx->mode && streq (ctx->mode, "chain-error") && ctx->rank == 0)
        return 0;
    if (!(out = call (ctx,
                      "jobdict.create",
                      "{s:s s:i s:[s]}",
                      "name", DICT_NAME,
                      "k", k,
                      "reach", "flux"))) {
        check_fail (ctx, "jobdict.create failed");
        return -1;
    }
    flux_plugin_arg_destroy (out);
    if (ctx->mode)
        return 0;
    /* A second dict with no reach list cannot see flux.* */
    if (!(out = call (ctx,
                      "jobdict.create",
                      "{s:s}",
                      "name", DICT_NAME "-noreach"))) {
        check_fail (ctx, "jobdict.create of noreach dict failed");
        return -1;
    }
    flux_plugin_arg_destroy (out);
    if (get_from (ctx, DICT_NAME "-noreach", "flux.taskmap", NULL) != 0) {
        check_fail (ctx, "flux.taskmap resolved without reach");
        return -1;
    }
    /* A bad reach list must be rejected */
    if ((out = call (ctx,
                     "jobdict.create",
                     "{s:s s:s}",
                     "name", "badreach",
                     "reach", "flux"))) {
        flux_plugin_arg_destroy (out);
        check_fail (ctx, "jobdict.create with non-array reach succeeded");
        return -1;
    }
    out = NULL;
    /* A name containing '.' must be rejected since it is the reach separator */
    if ((out = call (ctx, "jobdict.create", "{s:s}", "name", "bad.name"))) {
        flux_plugin_arg_destroy (out);
        check_fail (ctx, "jobdict.create with '.' in name succeeded");
        return -1;
    }
    out = NULL;
    /* An empty name must be rejected */
    if ((out = call (ctx, "jobdict.create", "{s:s}", "name", ""))) {
        flux_plugin_arg_destroy (out);
        check_fail (ctx, "jobdict.create with empty name succeeded");
        return -1;
    }
    out = NULL;

    /* A duplicate create must fail */
    if ((out = call (ctx, "jobdict.create", "{s:s}", "name", DICT_NAME))) {
        flux_plugin_arg_destroy (out);
        check_fail (ctx, "duplicate jobdict.create unexpectedly succeeded");
        return -1;
    }
    /* Ops on an unknown dict must fail */
    if ((out = call (ctx,
                     "jobdict.put",
                     "{s:s s:s s:s}",
                     "name", "nosuchdict",
                     "key", "a",
                     "value", "b"))) {
        flux_plugin_arg_destroy (out);
        check_fail (ctx, "put to unknown jobdict unexpectedly succeeded");
        return -1;
    }
    /* An exchange this shell is not part of must fail */
    if (ctx->size > 1 && ctx->rank == 0) {
        if ((out = call (ctx,
                         "jobdict.exchange",
                         "{s:s s:s s:s s:s}",
                         "name", DICT_NAME,
                         "tag", "notme",
                         "participants", "1",
                         "exit_topic", EXIT_TOPIC))) {
            flux_plugin_arg_destroy (out);
            check_fail (ctx, "exchange excluding self unexpectedly succeeded");
            return -1;
        }
    }
    /* An exchange naming a rank beyond the shell count must fail */
    {
        char ranks[32];
        snprintf (ranks, sizeof (ranks), "%d,%d", ctx->rank, ctx->size);
        if ((out = call (ctx,
                         "jobdict.exchange",
                         "{s:s s:s s:s s:s}",
                         "name", DICT_NAME,
                         "tag", "outofrange",
                         "participants", ranks,
                         "exit_topic", EXIT_TOPIC))) {
            flux_plugin_arg_destroy (out);
            check_fail (ctx, "exchange with bad rank unexpectedly succeeded");
            return -1;
        }
    }
    /* Get of a missing key succeeds with no value */
    if (expect_key (ctx, "nosuchkey", NULL) < 0)
        return -1;

    /* The flux dict is populated by the fluxkeys builtin before this
     * plugin's shell.init runs, and a name-qualified get through our own
     * dict resolves to it.  taskmap is always present; log it so the test
     * can compare it with the job's taskmap.  Also check that the same key
     * is readable directly from the flux dict without the prefix, and that
     * a prefix not in the reach list is not resolved.
     */
    {
        char *val = NULL;
        if (get (ctx, "flux.taskmap", &val) != 1) {
            check_fail (ctx, "flux.taskmap missing via qualified get");
            return -1;
        }
        shell_log ("flux.taskmap=%s", val);
        free (val);
        if (get_from (ctx, "flux", "taskmap", &val) != 1) {
            check_fail (ctx, "taskmap missing from flux jobdict");
            return -1;
        }
        free (val);
        if (get (ctx, "flux.instance-level", &val) != 1) {
            check_fail (ctx, "flux.instance-level missing via qualified get");
            return -1;
        }
        shell_log ("flux.instance-level=%s", val);
        free (val);
        if (expect_key (ctx, "nosuchdict.taskmap", NULL) < 0)
            return -1;
        /* Keys under a reach prefix are read-only, so a put cannot shadow
         * the flux dict.  A key with no subkey is not a reach key though.
         */
        if (put (ctx, "flux.taskmap", "bogus", 0) == 0
            || put (ctx, "flux.taskmap", "bogus", 1) == 0) {
            check_fail (ctx, "put of reach key flux.taskmap succeeded");
            return -1;
        }
        if (expect_key (ctx, "flux.nosuchkey", NULL) < 0)
            return -1;
        if (put (ctx, "flux.", "ok", 0) < 0
            || put (ctx, "fluxy.x", "ok", 0) < 0) {
            check_fail (ctx, "put of non-reach key failed");
            return -1;
        }
    }

    snprintf (key, sizeof (key), "global.%d", ctx->rank);
    snprintf (val, sizeof (val), "from-%d", ctx->rank);
    if (put (ctx, key, val, 0) < 0) {
        check_fail (ctx, "put %s failed", key);
        return -1;
    }
    snprintf (key, sizeof (key), "local.%d", ctx->rank);
    if (put (ctx, key, val, 1) < 0) {
        check_fail (ctx, "put local %s failed", key);
        return -1;
    }
    /* Pending and local keys are visible locally before any exchange */
    snprintf (key, sizeof (key), "global.%d", ctx->rank);
    if (expect_key (ctx, key, val) < 0)
        return -1;
    return 0;
}

/* Inter-shell RPCs require all shells to have registered their services,
 * which is only guaranteed once the shells exit the init barrier, so the
 * first exchange is entered from shell.post-init (the earliest callback
 * after that barrier).
 */
static int test_post_init (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *data)
{
    struct test_ctx *ctx = data;

    /* Keep the shell from exiting (the job may be a quick 'true') until the
     * final exchange checks have run on this shell.
     */
    if (ctx->mode && streq (ctx->mode, "chain-error") && ctx->rank == 0)
        return 0;
    if (flux_shell_add_completion_ref (ctx->shell, "jobdict-test") < 0) {
        check_fail (ctx, "flux_shell_add_completion_ref failed");
        return -1;
    }
    if (ctx->mode && streq (ctx->mode, "chain-error"))
        return exchange (ctx, "chain", NULL);
    if (ctx->mode && streq (ctx->mode, "eproto")) {
        /* Shells 1-N enter T first so their requests are queued on shell 0
         * before it enters T from its sync exit callback (per-peer FIFO).
         */
        if (ctx->rank > 0 && exchange (ctx, "T", NULL) < 0)
            return -1;
        return exchange (ctx, "sync", NULL);
    }
    if (ctx->mode && streq (ctx->mode, "concurrent")) {
        return (put_cc (ctx, "v1") < 0
                || exchange (ctx, "A", NULL) < 0
                || put_cc (ctx, "v2") < 0
                || exchange (ctx, "B", NULL) < 0) ? -1 : 0;
    }
    if (exchange (ctx, "full1", NULL) < 0) {
        check_fail (ctx, "exchange full1 failed");
        return -1;
    }
    return 0;
}

static void test_ctx_destroy (struct test_ctx *ctx)
{
    free (ctx);
}

int flux_plugin_init (flux_plugin_t *p)
{
    struct test_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return -1;
    ctx->shell = flux_plugin_get_shell (p);
    if (flux_plugin_aux_set (p, NULL, ctx, (flux_free_f)test_ctx_destroy) < 0) {
        free (ctx);
        return -1;
    }
    if (flux_plugin_set_name (p, FLUX_SHELL_PLUGIN_NAME) < 0
        || flux_plugin_add_handler (p, "shell.init", test_init, ctx) < 0
        || flux_plugin_add_handler (p, "shell.post-init", test_post_init, ctx)
           < 0
        || flux_plugin_add_handler (p, EXIT_TOPIC, exchange_exit, ctx) < 0)
        return -1;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
