/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* test/queues.c - unit tests for queues.[ch]
 *
 * Modeled on test/restart.c pattern.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "src/modules/job-manager/queues.h"

/* ---------- notify tracking helpers ----------------------------------- */

struct notify_record {
    char name[64];      /* queue name or "" for anon */
    char event[32];
};

#define MAX_NOTIFY 64
static struct notify_record notify_log[MAX_NOTIFY];
static int notify_count;

static void notify_reset (void)
{
    notify_count = 0;
}

static void notify_cb (struct queues *queues,
                       struct queue *q,
                       const char *event,
                       void *arg)
{
    if (notify_count < MAX_NOTIFY) {
        const char *n = queue_name (q);
        snprintf (notify_log[notify_count].name,
                  sizeof (notify_log[0].name),
                  "%s", n ? n : "");
        snprintf (notify_log[notify_count].event,
                  sizeof (notify_log[0].event),
                  "%s", event);
        notify_count++;
    }
}


/* ---------- tests ----------------------------------------------------- */

static void test_create_destroy (void)
{
    struct queues *qs;
    struct queue *q;

    qs = queues_create ();
    ok (qs != NULL, "queues_create works");

    /* Default state: anonymous queue, enabled + started */
    ok (!queues_have_named (qs),
        "queues_have_named returns false after create");

    q = queues_lookup (qs, NULL, NULL);
    ok (q != NULL, "queues_lookup NULL returns anon queue");

    ok (queue_name (q) == NULL,
        "anon queue has NULL name");
    ok (queue_is_enabled (q),
        "anon queue is enabled by default");
    ok (queue_is_started (q),
        "anon queue is started by default");
    ok (queue_disable_reason (q) == NULL,
        "anon queue disable_reason is NULL");
    ok (queue_stop_reason (q) == NULL,
        "anon queue stop_reason is NULL");
    ok (queue_requires (q) == NULL,
        "anon queue requires is NULL");

    queues_destroy (qs);
    pass ("queues_destroy on anon queue doesn't crash");

    queues_destroy (NULL);
    pass ("queues_destroy on NULL doesn't crash");
}

static void test_configure_named (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* Configure two named queues */
    config = json_pack ("{s:{} s:{}}",
                        "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");

    ok (queues_configure (qs, config, &error) == 0,
        "queues_configure with named queues works");
    ok (queues_have_named (qs),
        "queues_have_named returns true after configure");

    /* Named queues default to enabled + stopped */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "queues_lookup finds 'batch'");
    ok (queue_is_enabled (q), "batch queue is enabled");
    ok (!queue_is_started (q), "batch queue is stopped");

    q = queues_lookup (qs, "debug", &error);
    ok (q != NULL, "queues_lookup finds 'debug'");
    ok (queue_is_enabled (q), "debug queue is enabled");
    ok (!queue_is_started (q), "debug queue is stopped");

    json_decref (config);
    queues_destroy (qs);
}

static void test_configure_empty_is_anon (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* Configure with NULL (empty config): stays anon */
    ok (queues_configure (qs, NULL, &error) == 0,
        "queues_configure with NULL config works");
    ok (!queues_have_named (qs),
        "queues_have_named still false after NULL configure");
    q = queues_lookup (qs, NULL, NULL);
    ok (q != NULL && queue_name (q) == NULL,
        "anon queue present after NULL configure");

    /* Configure with empty object: stays anon */
    json_t *empty = json_object ();
    ok (queues_configure (qs, empty, &error) == 0,
        "queues_configure with empty object works");
    ok (!queues_have_named (qs),
        "queues_have_named still false after empty configure");
    json_decref (empty);

    queues_destroy (qs);
}

static void test_anon_to_named_and_back (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    /* Anon -> named */
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");

    ok (queues_configure (qs, config, &error) == 0,
        "anon->named configure works");
    ok (queues_have_named (qs),
        "now have named queues");
    json_decref (config);

    /* named -> anon */
    ok (queues_configure (qs, NULL, &error) == 0,
        "named->anon configure works");
    ok (!queues_have_named (qs),
        "back to anon");
    q = queues_lookup (qs, NULL, NULL);
    ok (q != NULL && queue_name (q) == NULL,
        "anon queue present");
    ok (queue_is_enabled (q) && queue_is_started (q),
        "new anon queue is enabled+started");

    queues_destroy (qs);
}

static void test_reload_diff (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* Initial configure: batch, debug */
    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "initial configure: batch+debug");
    json_decref (config);

    /* Mark batch queue with some state to test preservation */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "found batch queue");
    ok (queue_start (q, false) == 0, "start batch queue");
    ok (queue_disable (q, "maintenance") == 0, "disable batch queue");

    /* Reload: batch stays, debug gone, new 'gpu' added */
    config = json_pack ("{s:{} s:{}}", "batch", "gpu");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload configure: batch+gpu");
    json_decref (config);

    /* batch should still exist with preserved state */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch survives reload");
    ok (!queue_is_enabled (q), "batch disable state preserved");
    ok (queue_is_started (q), "batch started state preserved");
    is (queue_disable_reason (q), "maintenance",
        "batch disable_reason preserved");

    /* debug should be gone */
    q = queues_lookup (qs, "debug", &error);
    ok (q == NULL, "debug removed on reload");

    /* gpu should exist, default state */
    q = queues_lookup (qs, "gpu", &error);
    ok (q != NULL, "gpu added on reload");
    ok (queue_is_enabled (q) && !queue_is_started (q),
        "gpu default state: enabled+stopped");

    queues_destroy (qs);
}

static void test_update_preserves_state (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *req1, *req2;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* Configure with requires */
    req1 = json_pack ("[s]", "partition:batch");
    if (!req1)
        BAIL_OUT ("json_pack failed");
    config = json_pack ("{s:{s:O}}", "batch", "requires", req1);
    json_decref (req1);
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch with requires");
    json_decref (config);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "found batch queue");
    ok (queue_requires (q) != NULL, "batch has requires");

    /* Set some admin state */
    ok (queue_disable (q, "down for maintenance") == 0,
        "disable batch queue");
    ok (queue_start (q, false) == 0, "start batch queue");

    /* Now reload with different requires */
    req2 = json_pack ("[s]", "partition:new-batch");
    if (!req2)
        BAIL_OUT ("json_pack failed");
    config = json_pack ("{s:{s:O}}", "batch", "requires", req2);
    json_decref (req2);
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload with updated requires");
    json_decref (config);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch still exists after reload");

    /* Requires should be updated */
    json_t *reqs = queue_requires (q);
    ok (reqs != NULL, "requires still set after update");
    ok (json_array_size (reqs) == 1, "requires has one element");
    const char *s = json_string_value (json_array_get (reqs, 0));
    is (s, "partition:new-batch",
        "requires updated to new value");

    /* Admin state preserved */
    ok (!queue_is_enabled (q), "disable state preserved");
    ok (queue_is_started (q), "start state preserved");
    is (queue_disable_reason (q), "down for maintenance",
        "disable_reason preserved");

    /* Reload with requires omitted: requires is cleared, admin state kept */
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload with requires omitted");
    json_decref (config);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch still exists after reload");
    ok (queue_requires (q) == NULL,
        "requires cleared when omitted from config");
    ok (!queue_is_enabled (q) && queue_is_started (q),
        "admin state preserved across requires clear");
    is (queue_disable_reason (q), "down for maintenance",
        "disable_reason preserved across requires clear");

    queues_destroy (qs);
}

static void test_lookup (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* Anon: lookup NULL succeeds */
    q = queues_lookup (qs, NULL, &error);
    ok (q != NULL, "lookup NULL in anon mode succeeds");

    /* Anon: lookup named fails */
    q = queues_lookup (qs, "batch", &error);
    ok (q == NULL, "lookup named in anon mode fails");
    ok (strlen (error.text) > 0, "error.text set");

    /* Named mode */
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure named queues");
    json_decref (config);

    /* Named: lookup by name succeeds */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "lookup 'batch' in named mode succeeds");

    /* Named: lookup NULL fails with "a named queue is required" */
    q = queues_lookup (qs, NULL, &error);
    ok (q == NULL, "lookup NULL in named mode fails");
    ok (strstr (error.text, "named queue") != NULL,
        "error says 'named queue'");

    /* Named: lookup unknown name fails */
    q = queues_lookup (qs, "nosuchqueue", &error);
    ok (q == NULL, "lookup unknown queue fails");
    ok (strlen (error.text) > 0, "error.text set");

    queues_destroy (qs);
}

static void test_enable_disable (void)
{
    struct queues *qs;
    struct queue *q;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);

    q = queues_lookup (qs, NULL, NULL);
    ok (q != NULL, "got anon queue");

    notify_reset ();
    ok (queue_disable (q, "testing") == 0, "queue_disable works");
    ok (!queue_is_enabled (q), "queue now disabled");
    is (queue_disable_reason (q), "testing",
        "disable reason set");
    ok (notify_count == 1
        && streq (notify_log[0].event, "disable"),
        "disable event fired");

    notify_reset ();
    ok (queue_enable (q) == 0, "queue_enable works");
    ok (queue_is_enabled (q), "queue now enabled");
    ok (queue_disable_reason (q) == NULL, "disable reason cleared");
    ok (notify_count == 1
        && streq (notify_log[0].event, "enable"),
        "enable event fired");

    /* A NULL reason must not crash (strdup(NULL)); the queue is
     * disabled with no reason string.  Reachable via a checkpoint
     * entry that has enable=false with no disable_reason.
     */
    ok (queue_disable (q, NULL) == 0, "queue_disable with NULL reason works");
    ok (!queue_is_enabled (q), "queue disabled with NULL reason");
    ok (queue_disable_reason (q) == NULL, "disable reason is NULL");

    queues_destroy (qs);
}

static void test_start_stop (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);

    /* Use named queue (starts stopped) */
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    queues_configure (qs, config, &error);
    json_decref (config);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL && !queue_is_started (q),
        "batch queue starts stopped");

    /* Stop with reason, no nocheckpoint */
    notify_reset ();
    ok (queue_stop (q, "down", false) == 0, "queue_stop works");
    ok (!queue_is_started (q), "queue stopped");
    is (queue_stop_reason (q), "down",
        "stop reason set");
    ok (notify_count == 1
        && streq (notify_log[0].event, "stop"),
        "stop event fired");

    /* Start */
    notify_reset ();
    ok (queue_start (q, false) == 0, "queue_start works");
    ok (queue_is_started (q), "queue started");
    ok (queue_stop_reason (q) == NULL, "stop reason cleared");
    ok (notify_count == 1
        && streq (notify_log[0].event, "start"),
        "start event fired");

    /* Nocheckpoint: stop without updating sticky */
    ok (queue_stop (q, "transient", true) == 0,
        "queue_stop with nocheckpoint works");
    ok (!queue_is_started (q), "queue stopped");

    /* Save should reflect sticky=true (was started before nocheckpoint stop) */
    json_t *saved = queues_save (qs);
    ok (saved != NULL, "queues_save works");
    int start_val = 0;
    ok (json_array_size (saved) == 1, "saved has one entry");
    ok (json_unpack (json_array_get (saved, 0), "{s:b}", "start", &start_val) == 0
        && start_val == 1,
        "nocheckpoint stop: sticky bit is still 'started'");
    json_decref (saved);

    queues_destroy (qs);
}

static void test_save_restore_v1 (void)
{
    struct queues *qs_save;
    struct queues *qs_restore;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *saved;

    /* Create queues and set state */
    qs_save = queues_create ();
    if (!qs_save)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs_save, config, &error) == 0,
        "configure batch+debug for save");
    json_decref (config);

    q = queues_lookup (qs_save, "batch", &error);
    ok (q != NULL, "batch lookup ok");
    ok (queue_disable (q, "maint") == 0, "disable batch");
    ok (queue_start (q, false) == 0, "start batch");

    q = queues_lookup (qs_save, "debug", &error);
    ok (q != NULL, "debug lookup ok");
    /* debug: enabled, stopped (default), with reason */
    ok (queue_stop (q, "offline", false) == 0, "stop debug");

    saved = queues_save (qs_save);
    ok (saved != NULL, "queues_save works");
    ok (json_is_array (saved), "saved is array");
    ok (json_array_size (saved) == 2, "saved has 2 entries");
    queues_destroy (qs_save);

    /* Restore into fresh queues with same config */
    qs_restore = queues_create ();
    if (!qs_restore)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs_restore, config, &error) == 0,
        "configure batch+debug for restore");
    json_decref (config);

    ok (queues_restore (qs_restore, 1, saved) == 0,
        "queues_restore v1 works");
    json_decref (saved);

    q = queues_lookup (qs_restore, "batch", &error);
    ok (q != NULL, "batch found after restore");
    ok (!queue_is_enabled (q), "batch disabled after restore");
    is (queue_disable_reason (q), "maint",
        "batch disable_reason restored");
    ok (queue_is_started (q), "batch started after restore");

    q = queues_lookup (qs_restore, "debug", &error);
    ok (q != NULL, "debug found after restore");
    ok (queue_is_enabled (q), "debug enabled after restore");
    ok (!queue_is_started (q), "debug stopped after restore");
    is (queue_stop_reason (q), "offline",
        "debug stop_reason restored");

    queues_destroy (qs_restore);
}

static void test_restore_v0 (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *saved;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch for v0 restore");
    json_decref (config);

    /* v0 only has enable/name/reason fields */
    saved = json_pack ("[{s:s s:b s:s}]",
                       "name", "batch",
                       "enable", 0,
                       "reason", "old-style-reason");
    if (!saved)
        BAIL_OUT ("json_pack for v0 data failed");

    ok (queues_restore (qs, 0, saved) == 0,
        "queues_restore v0 works");
    json_decref (saved);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch found after v0 restore");
    ok (!queue_is_enabled (q), "batch disabled after v0 restore");
    is (queue_disable_reason (q), "old-style-reason",
        "v0 'reason' key used as disable_reason");

    queues_destroy (qs);
}

static void test_restore_unknown_queue_ignored (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;
    json_t *saved;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch only");
    json_decref (config);

    /* v1 restore with unknown queue entry */
    saved = json_pack ("[{s:s s:b s:b}]",
                       "name", "nosuchqueue",
                       "enable", 1,
                       "start", 0);
    if (!saved)
        BAIL_OUT ("json_pack failed");

    ok (queues_restore (qs, 1, saved) == 0,
        "queues_restore ignores unknown queue entry");
    json_decref (saved);

    queues_destroy (qs);
}

static void test_notify_configure (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    /* Configure named queues: should fire "add" for each */
    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure fires add events");
    json_decref (config);

    /* anon->named transition fires "remove" for anon plus one "add"
     * per named queue */
    ok (notify_count == 3, "three events fired");
    bool saw_anon_remove = false;
    bool saw_batch_add = false, saw_debug_add = false;
    for (int i = 0; i < notify_count; i++) {
        if (streq (notify_log[i].name, "")
            && streq (notify_log[i].event, "remove"))
            saw_anon_remove = true;
        if (streq (notify_log[i].name, "batch")
            && streq (notify_log[i].event, "add"))
            saw_batch_add = true;
        if (streq (notify_log[i].name, "debug")
            && streq (notify_log[i].event, "add"))
            saw_debug_add = true;
    }
    ok (saw_anon_remove, "anon remove event fired");
    ok (saw_batch_add, "batch add event fired");
    ok (saw_debug_add, "debug add event fired");

    /* Reload removing debug: should fire "remove" for debug,
     * "update" for batch */
    notify_reset ();
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload fires remove+update events");
    json_decref (config);

    bool saw_debug_remove = false, saw_batch_update = false;
    for (int i = 0; i < notify_count; i++) {
        if (streq (notify_log[i].name, "debug")
            && streq (notify_log[i].event, "remove"))
            saw_debug_remove = true;
        if (streq (notify_log[i].name, "batch")
            && streq (notify_log[i].event, "update"))
            saw_batch_update = true;
    }
    ok (saw_debug_remove, "debug remove event fired on reload");
    ok (saw_batch_update, "batch update event fired on reload");

    queues_destroy (qs);
}

static void test_notify_restore (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *saved;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch");
    json_decref (config);

    /* Disable + stop with reason */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch found");
    queue_disable (q, "maint");
    queue_stop (q, "down", false);

    saved = queues_save (qs);
    ok (saved != NULL, "save ok");
    queues_destroy (qs);

    /* New queues, register notify, then restore */
    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    queues_configure (qs, config, &error);
    json_decref (config);

    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    ok (queues_restore (qs, 1, saved) == 0,
        "queues_restore works");
    json_decref (saved);

    /* Restore reconstructs previously-notified state: no events */
    ok (notify_count == 0,
        "no notify events fired during restore");

    /* State was nonetheless applied */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL
        && !queue_is_enabled (q)
        && !queue_is_started (q),
        "restored state applied without notifications");

    queues_destroy (qs);
}

static void test_requires_accessor (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *req;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    req = json_pack ("[s s]", "partition:batch", "infiniband");
    if (!req)
        BAIL_OUT ("json_pack failed");
    config = json_pack ("{s:{s:O}}", "batch", "requires", req);
    json_decref (req);
    if (!config)
        BAIL_OUT ("json_pack failed");

    ok (queues_configure (qs, config, &error) == 0,
        "configure batch with requires array");
    json_decref (config);

    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch found");

    json_t *r = queue_requires (q);
    ok (r != NULL, "queue_requires returns non-NULL");
    ok (json_is_array (r), "queue_requires returns array");
    ok (json_array_size (r) == 2, "requires has 2 elements");
    ok (streq (json_string_value (json_array_get (r, 0)),
                "partition:batch"),
        "first requires element correct");
    ok (streq (json_string_value (json_array_get (r, 1)),
                "infiniband"),
        "second requires element correct");

    queues_destroy (qs);
}

static void test_restore_bad_args (void)
{
    struct queues *qs;
    json_t *a;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    a = json_array ();
    if (!a)
        BAIL_OUT ("json_array failed");

    errno = 0;
    ok (queues_restore (qs, 2, a) < 0 && errno == EINVAL,
        "queues_restore with invalid version fails");
    errno = 0;
    ok (queues_restore (qs, 1, NULL) < 0 && errno == EINVAL,
        "queues_restore with NULL data fails");
    errno = 0;
    ok (queues_restore (qs, 1, json_null ()) < 0 && errno == EINVAL,
        "queues_restore with non-array fails");

    json_decref (a);
    queues_destroy (qs);
}

static void test_anon_restore_v1 (void)
{
    struct queues *qs_save, *qs_restore;
    struct queue *q;
    json_t *saved;

    /* Save anon queue state */
    qs_save = queues_create ();
    if (!qs_save)
        BAIL_OUT ("queues_create failed");
    q = queues_lookup (qs_save, NULL, NULL);
    ok (q != NULL, "got anon queue");
    ok (queue_disable (q, "anon-maint") == 0, "disable anon queue");
    ok (queue_stop (q, "anon-down", false) == 0, "stop anon queue");

    saved = queues_save (qs_save);
    ok (saved != NULL, "save anon queue state");
    ok (json_array_size (saved) == 1, "one entry saved");
    queues_destroy (qs_save);

    /* Restore */
    qs_restore = queues_create ();
    if (!qs_restore)
        BAIL_OUT ("queues_create failed");

    ok (queues_restore (qs_restore, 1, saved) == 0,
        "restore anon queue state v1");
    json_decref (saved);

    q = queues_lookup (qs_restore, NULL, NULL);
    ok (q != NULL && queue_name (q) == NULL,
        "anon queue present after restore");
    ok (!queue_is_enabled (q), "anon disabled after restore");
    is (queue_disable_reason (q), "anon-maint",
        "anon disable_reason restored");
    ok (!queue_is_started (q), "anon stopped after restore");
    is (queue_stop_reason (q), "anon-down",
        "anon stop_reason restored");

    queues_destroy (qs_restore);
}

static void test_admin_ops (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);
    queues_set_notify (qs, notify_cb, NULL);

    /* start one queue by name */
    notify_reset ();
    ok (queues_start_queue (qs, "batch", false, &error) == 0,
        "queues_start_queue by name works");
    q = queues_lookup (qs, "batch", &error);
    ok (q && queue_is_started (q),
        "batch is started");
    q = queues_lookup (qs, "debug", &error);
    ok (q && !queue_is_started (q),
        "debug remains stopped");
    ok (notify_count == 1
        && streq (notify_log[0].event, "start")
        && streq (notify_log[0].name, "batch"),
        "one start event fired for batch");

    /* stop one queue by name with a reason */
    notify_reset ();
    ok (queues_stop_queue (qs, "batch", "wedged", false, &error) == 0,
        "queues_stop_queue by name works");
    q = queues_lookup (qs, "batch", &error);
    ok (q
        && !queue_is_started (q)
        && streq (queue_stop_reason (q), "wedged"),
        "batch stopped by name with reason");
    q = queues_lookup (qs, "debug", &error);
    ok (q && !queue_is_started (q), "debug remains stopped");
    ok (notify_count == 1
        && streq (notify_log[0].event, "stop")
        && streq (notify_log[0].name, "batch"),
        "one stop event fired for batch");

    /* stop all queues (NULL name) with reason */
    notify_reset ();
    ok (queues_stop_queue (qs, NULL, "maint", false, &error) == 0,
        "queues_stop_queue with NULL name works");
    q = queues_lookup (qs, "batch", &error);
    ok (q
        && !queue_is_started (q)
        && streq (queue_stop_reason (q), "maint"),
        "batch is stopped with reason");
    ok (notify_count == 2,
        "stop events fired for both queues");

    /* unknown name fails */
    ok (queues_start_queue (qs, "noexist", false, &error) < 0,
        "queues_start_queue unknown name fails");

    /* disable requires a reason */
    errno = 0;
    ok (queues_disable_queue (qs, "batch", NULL, &error) < 0
        && errno == EINVAL,
        "queues_disable_queue without reason fails with EINVAL");

    /* disable by name, then enable all */
    ok (queues_disable_queue (qs, "batch", "broken", &error) == 0,
        "queues_disable_queue works");
    q = queues_lookup (qs, "batch", &error);
    ok (q
        && !queue_is_enabled (q)
        && streq (queue_disable_reason (q), "broken"),
        "batch is disabled with reason");
    ok (queues_enable_queue (qs, NULL, &error) == 0,
        "queues_enable_queue with NULL name works");
    ok (q && queue_is_enabled (q),
        "batch is enabled again");

    queues_destroy (qs);
}

/* Notify callback that reenters the class with a lookup, as the job
 * manager's callback does (its enqueue/dequeue side effects match jobs
 * by queue name).  zhashx_lookup repositions the hash cursor, so
 * aggregate operations must not iterate with queues_first/next while
 * notifications fire - this callback turns that mistake into an
 * infinite loop.  Regression test for a bug caught in testing.
 */
static void reenter_cb (struct queues *queues,
                        struct queue *q,
                        const char *event,
                        void *arg)
{
    int *count = arg;

    (void)queues_lookup (queues, queue_name (q), NULL);
    (*count)++;
}

static void test_admin_ops_reenter (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    int count = 0;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    config = json_pack ("{s:{} s:{} s:{}}", "batch", "debug", "gpu");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);
    queues_set_notify (qs, reenter_cb, &count);

    count = 0;
    ok (queues_start_queue (qs, NULL, false, &error) == 0,
        "queues_start_queue all with reentrant callback works");
    ok (count == 3,
        "start fired exactly one notification per queue");
    q = queues_lookup (qs, "batch", &error);
    ok (q && queue_is_started (q),
        "batch is started");
    q = queues_lookup (qs, "gpu", &error);
    ok (q && queue_is_started (q),
        "gpu is started");

    count = 0;
    ok (queues_stop_queue (qs, NULL, "maint", false, &error) == 0,
        "queues_stop_queue all with reentrant callback works");
    ok (count == 3,
        "stop fired exactly one notification per queue");

    count = 0;
    ok (queues_disable_queue (qs, NULL, "maint", &error) == 0
        && queues_enable_queue (qs, NULL, &error) == 0,
        "disable/enable all with reentrant callback works");
    ok (count == 6,
        "disable+enable fired exactly one notification per queue each");

    queues_destroy (qs);
}

static void test_list_names (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;
    zlistx_t *names;
    const char *name;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* anon mode: empty list */
    names = queues_list_names (qs);
    ok (names != NULL && zlistx_size (names) == 0,
        "queues_list_names returns empty list in anon mode");
    zlistx_destroy (&names);

    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);

    names = queues_list_names (qs);
    ok (names != NULL && zlistx_size (names) == 2,
        "queues_list_names returns both queue names");
    bool saw_batch = false, saw_debug = false;
    name = zlistx_first (names);
    while (name) {
        if (streq (name, "batch"))
            saw_batch = true;
        if (streq (name, "debug"))
            saw_debug = true;
        name = zlistx_next (names);
    }
    ok (saw_batch && saw_debug,
        "list contains batch and debug");

    /* snapshot survives queue removal mid-iteration */
    name = zlistx_first (names);
    ok (queues_remove (qs, "batch", &error) == 0,
        "queues_remove during snapshot iteration works");
    ok (queues_lookup (qs, "batch", NULL) == NULL,
        "removed name no longer resolves");
    ok (zlistx_size (names) == 2,
        "snapshot list is unaffected by removal");
    zlistx_destroy (&names);

    queues_destroy (qs);
}

static void test_add_remove_primitives (void)
{
    struct queues *qs;
    struct queue *q;
    struct queue *q2;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);

    /* add named queue from anon mode */
    notify_reset ();
    config = json_pack ("{s:[s]}", "requires", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    q = queues_add (qs, "batch", config, &error);
    json_decref (config);
    ok (q != NULL, "queues_add batch from anon mode works");
    ok (queues_have_named (qs), "queues_have_named is true after add");
    ok (notify_count == 2
        && streq (notify_log[0].event, "remove")
        && streq (notify_log[0].name, "")
        && streq (notify_log[1].event, "add")
        && streq (notify_log[1].name, "batch"),
        "add from anon mode fires anon remove then batch add");
    ok (queue_is_enabled (q) && !queue_is_started (q),
        "added named queue is enabled + stopped");
    ok (queue_requires (q) != NULL
        && json_array_size (queue_requires (q)) == 1,
        "added queue has requires from config");

    /* duplicate add is an error */
    notify_reset ();
    errno = 0;
    q2 = queues_add (qs, "batch", NULL, &error);
    ok (q2 == NULL && errno == EEXIST,
        "queues_add duplicate name fails with EEXIST");
    ok (notify_count == 0, "failed add fires no events");

    /* add a second queue in named mode: no mode transition */
    notify_reset ();
    q2 = queues_add (qs, "debug", NULL, &error);
    ok (q2 != NULL, "queues_add debug in named mode works");
    ok (notify_count == 1
        && streq (notify_log[0].event, "add")
        && streq (notify_log[0].name, "debug"),
        "add in named mode fires a single add event");

    /* remove error cases */
    errno = 0;
    ok (queues_remove (qs, "noexist", &error) < 0 && errno == ENOENT,
        "queues_remove unknown name fails with ENOENT");
    errno = 0;
    ok (queues_remove (qs, NULL, &error) < 0 && errno == EINVAL,
        "queues_remove NULL name fails with EINVAL");

    /* remove one queue */
    notify_reset ();
    ok (queues_remove (qs, "debug", &error) == 0,
        "queues_remove debug works");
    ok (notify_count == 1
        && streq (notify_log[0].event, "remove")
        && streq (notify_log[0].name, "debug"),
        "remove fires a single remove event");
    ok (queues_lookup (qs, "debug", NULL) == NULL,
        "removed queue is no longer found");

    /* add NULL from named mode: back to anon */
    notify_reset ();
    q = queues_add (qs, NULL, NULL, &error);
    ok (q != NULL, "queues_add NULL from named mode works");
    ok (!queues_have_named (qs), "queues_have_named is false again");
    ok (queue_is_enabled (q) && queue_is_started (q),
        "anon queue is enabled + started");
    ok (notify_count == 2
        && streq (notify_log[0].event, "remove")
        && streq (notify_log[0].name, "batch")
        && streq (notify_log[1].event, "add")
        && streq (notify_log[1].name, ""),
        "add NULL fires remove per named queue then anon add");

    /* add NULL when already anon: idempotent, no events */
    notify_reset ();
    q2 = queues_add (qs, NULL, NULL, &error);
    ok (q2 == q, "queues_add NULL when already anon returns same queue");
    ok (notify_count == 0, "idempotent anon add fires no events");

    /* remove in anon mode is an error */
    errno = 0;
    ok (queues_remove (qs, "batch", &error) < 0 && errno == EINVAL,
        "queues_remove in anon mode fails with EINVAL");

    queues_destroy (qs);
}

/* queues_add (NULL) with multiple named queues: all named queues are
 * removed (one "remove" notification each, in any order) and replaced
 * by a fresh anonymous queue, regardless of the named queues'
 * administrative state.
 */
static void test_add_anon_removes_all (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    config = json_pack ("{s:{} s:{} s:{}}", "batch", "debug", "gpu");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);

    /* Give the queues distinct administrative state */
    q = queues_lookup (qs, "batch", &error);
    if (!q || queue_start (q, false) < 0)
        BAIL_OUT ("could not start batch");
    q = queues_lookup (qs, "debug", &error);
    if (!q || queue_disable (q, "maint") < 0)
        BAIL_OUT ("could not disable debug");

    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    q = queues_add (qs, NULL, NULL, &error);
    ok (q != NULL,
        "queues_add NULL with multiple named queues works");
    ok (!queues_have_named (qs),
        "table is in anon mode");
    ok (queue_is_enabled (q) && queue_is_started (q),
        "anon queue is enabled + started regardless of prior state");

    /* One remove per named queue (hash order unspecified), then the
     * anon add */
    int removes = 0;
    bool saw_batch = false, saw_debug = false, saw_gpu = false;
    for (int i = 0; i < notify_count; i++) {
        if (streq (notify_log[i].event, "remove")) {
            removes++;
            if (streq (notify_log[i].name, "batch"))
                saw_batch = true;
            if (streq (notify_log[i].name, "debug"))
                saw_debug = true;
            if (streq (notify_log[i].name, "gpu"))
                saw_gpu = true;
        }
    }
    ok (removes == 3 && saw_batch && saw_debug && saw_gpu,
        "remove fired once for each named queue");
    ok (notify_count == 4
        && streq (notify_log[3].event, "add")
        && streq (notify_log[3].name, ""),
        "anon add fired last");

    ok (queues_lookup (qs, "batch", NULL) == NULL
        && queues_lookup (qs, "debug", NULL) == NULL
        && queues_lookup (qs, "gpu", NULL) == NULL,
        "named queues are no longer found");
    {
        zlistx_t *names = queues_list_names (qs);
        ok (names != NULL && zlistx_size (names) == 0
            && queues_lookup (qs, NULL, NULL) == q,
            "table contains exactly the anonymous queue");
        zlistx_destroy (&names);
    }

    queues_destroy (qs);
}

/* A new queue whose config value is not an object (so the "requires"
 * unpack fails) is rejected with EINVAL, and the prior anon state is
 * left intact.
 */
static void test_add_invalid_config (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    /* config value is an integer, not an object */
    config = json_integer (5);
    if (!config)
        BAIL_OUT ("json_integer failed");
    errno = 0;
    q = queues_add (qs, "batch", config, &error);
    json_decref (config);
    ok (q == NULL && errno == EINVAL,
        "queues_add with non-object config fails with EINVAL");
    ok (strlen (error.text) > 0, "error.text set");
    ok (notify_count == 0, "failed add fires no events");
    ok (!queues_have_named (qs), "still in anon mode after failed add");

    queues_destroy (qs);
}

/* Reconfiguring an existing queue with an invalid (non-object) config
 * value takes the queues_update() path and fails with EINVAL. The prior
 * queue and its administrative state must be left intact.
 */
static void test_update_invalid_config (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *bad;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{s:[s]}}", "batch", "requires", "gpu");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch with requires");
    json_decref (config);

    /* Give batch administrative state to verify it survives the failure */
    q = queues_lookup (qs, "batch", &error);
    if (!q || queue_disable (q, "maint") < 0)
        BAIL_OUT ("could not disable batch");

    /* reconfigure existing "batch" with a non-object value */
    bad = json_pack ("{s:i}", "batch", 5);
    if (!bad)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_configure (qs, bad, &error) < 0 && errno == EINVAL,
        "reconfigure existing queue with non-object value fails EINVAL");
    ok (strlen (error.text) > 0, "error.text set");
    json_decref (bad);

    /* Old configuration must be intact after the failed reconfigure */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch still present after failed reconfigure");
    ok (!queue_is_enabled (q), "batch still disabled after failure");
    is (queue_disable_reason (q), "maint",
        "batch disable_reason preserved after failure");
    ok (queue_requires (q) != NULL
        && json_array_size (queue_requires (q)) == 1,
        "batch requires preserved after failure");

    queues_destroy (qs);
}

/* A failed queues_configure() must not partially apply: a config that
 * drops one queue and adds an invalid one is rejected whole, leaving the
 * dropped queue in place with no notifications fired. Regression test for
 * the non-transactional remove-before-add ordering.
 */
static void test_configure_partial_failure (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;
    json_t *bad;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);

    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    /* New config drops "debug" and adds invalid "gpu" (non-object). The
     * whole reconfigure must be rejected before "debug" is removed.
     */
    bad = json_pack ("{s:{} s:i}", "batch", "gpu", 5);
    if (!bad)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_configure (qs, bad, &error) < 0 && errno == EINVAL,
        "configure with an invalid new queue fails with EINVAL");
    json_decref (bad);

    ok (notify_count == 0, "no notifications fired on failed configure");
    ok (queues_lookup (qs, "debug", NULL) != NULL,
        "dropped queue still present after failed configure");
    ok (queues_lookup (qs, "batch", NULL) != NULL,
        "existing queue still present after failed configure");
    ok (queues_lookup (qs, "gpu", NULL) == NULL,
        "invalid queue was not added");

    queues_destroy (qs);
}

/* A malformed entry inside the restore array (missing a required field)
 * fails with EINVAL. Covers the per-entry unpack error in restore_v1
 * and restore_v0.
 */
static void test_restore_bad_entry (void)
{
    struct queues *qs;
    json_t *saved;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* v1 entry missing the required "start" field */
    saved = json_pack ("[{s:s s:b}]",
                       "name", "batch",
                       "enable", 1);
    if (!saved)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_restore (qs, 1, saved) < 0 && errno == EINVAL,
        "queues_restore v1 with malformed entry fails with EINVAL");
    json_decref (saved);

    /* v0 entry missing the required "enable" field */
    saved = json_pack ("[{s:s}]", "name", "batch");
    if (!saved)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_restore (qs, 0, saved) < 0 && errno == EINVAL,
        "queues_restore v0 with malformed entry fails with EINVAL");
    json_decref (saved);

    queues_destroy (qs);
}

static void test_status_encode (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;
    json_t *o;
    int enable;
    int start;
    const char *blocked;
    const char *reason;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    config = json_pack ("{s:{}}", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);
    q = queues_lookup (qs, "batch", &error);
    if (!q)
        BAIL_OUT ("queues_lookup failed");

    /* enabled + stopped (default), scheduler ready, no stop reason */
    o = queue_status_encode (q, true);
    ok (o != NULL, "queue_status_encode works");
    ok (json_unpack (o, "{s:b s:b !}", "enable", &enable,
                     "start", &start) == 0
        && enable && !start,
        "default status: enabled + stopped, no reasons");
    json_decref (o);

    /* stopped with reason */
    queue_stop (q, "maintenance", false);
    o = queue_status_encode (q, true);
    ok (o && json_unpack (o, "{s:b s:b s:s !}", "enable", &enable,
                          "start", &start, "stop_reason", &reason) == 0
        && enable && !start && streq (reason, "maintenance"),
        "stopped status includes stop_reason");
    json_decref (o);

    /* started, scheduler ready */
    queue_start (q, false);
    o = queue_status_encode (q, true);
    ok (o && json_unpack (o, "{s:b s:b !}", "enable", &enable,
                          "start", &start) == 0
        && enable && start,
        "started status: start true, no stop_reason");
    json_decref (o);

    /* started but scheduler offline: presented stopped + blocked +
     * synthesized reason (own state untouched) */
    o = queue_status_encode (q, false);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "blocked", &blocked,
                        "stop_reason", &reason) == 0
        && enable
        && !start
        && streq (blocked, "scheduler")
        && streq (reason, "Scheduler is offline"),
        "sched offline: presented stopped and blocked with reason");
    ok (queue_is_started (q),
        "sched offline does not modify queue state");
    json_decref (o);

    /* disabled: disable_reason included */
    queue_disable (q, "no submit");
    o = queue_status_encode (q, true);
    ok (o && json_unpack (o, "{s:b s:b s:s !}", "enable", &enable,
                          "start", &start, "disable_reason", &reason) == 0
        && !enable && start && streq (reason, "no submit"),
        "disabled status includes disable_reason");
    json_decref (o);

    queues_destroy (qs);
}

static void test_list_encode (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;
    json_t *a;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* anon mode: empty array */
    a = queues_list_encode (qs);
    ok (a != NULL && json_is_array (a) && json_array_size (a) == 0,
        "list_encode in anon mode returns empty array");
    json_decref (a);

    /* named mode: array of names */
    config = json_pack ("{s:{} s:{}}", "batch", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);

    a = queues_list_encode (qs);
    ok (a != NULL && json_array_size (a) == 2,
        "list_encode returns both queue names");
    bool saw_batch = false, saw_debug = false;
    size_t i;
    json_t *v;
    json_array_foreach (a, i, v) {
        const char *s = json_string_value (v);
        if (s && streq (s, "batch"))
            saw_batch = true;
        if (s && streq (s, "debug"))
            saw_debug = true;
    }
    ok (saw_batch && saw_debug, "list contains batch and debug");
    json_decref (a);

    queues_destroy (qs);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create_destroy ();
    test_configure_named ();
    test_configure_empty_is_anon ();
    test_anon_to_named_and_back ();
    test_reload_diff ();
    test_update_preserves_state ();
    test_lookup ();
    test_enable_disable ();
    test_start_stop ();
    test_save_restore_v1 ();
    test_restore_v0 ();
    test_restore_unknown_queue_ignored ();
    test_notify_configure ();
    test_notify_restore ();
    test_requires_accessor ();
    test_restore_bad_args ();
    test_anon_restore_v1 ();
    test_add_remove_primitives ();
    test_add_anon_removes_all ();
    test_add_invalid_config ();
    test_update_invalid_config ();
    test_configure_partial_failure ();
    test_restore_bad_entry ();
    test_admin_ops ();
    test_admin_ops_reenter ();
    test_list_names ();
    test_status_encode ();
    test_list_encode ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
