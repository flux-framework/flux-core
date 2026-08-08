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

/* A failed queues_configure() must not partially apply when the failure is
 * an unresolvable virtual-queue parent (RFC 33), and in particular must not
 * leave a sibling virtual queue's ->parent dangling. The bad new config here
 * drops "batch" (which "vq" is parented to) and adds "aaa" naming a missing
 * parent; "aaa" sorts before "vq" so a per-entry resolve would fail after
 * "batch" was already freed, leaving vq->parent pointing at freed memory.
 * The whole-config pre-check must reject this before any queue is touched.
 * This path is unreachable via the broker (conf_policy_validate() rejects it
 * first) but the class must be self-protecting against direct input.
 */
static void test_configure_bad_parent_transactional (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *vq;
    flux_error_t error;
    json_t *config;
    json_t *bad;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{s:s}}",
                        "batch",
                        "vq",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed");
    json_decref (config);

    batch = queues_lookup (qs, "batch", NULL);
    vq = queues_lookup (qs, "vq", NULL);
    if (!batch || !vq || queue_parent (vq) != batch)
        BAIL_OUT ("initial vqueue config not as expected");

    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    /* aaa names a missing parent; batch (vq's parent) is dropped. */
    bad = json_pack ("{s:{s:s} s:{s:s}}",
                     "aaa",
                       "parent", "nosuchqueue",
                     "vq",
                       "parent", "batch");
    if (!bad)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_configure (qs, bad, &error) < 0 && errno == EINVAL,
        "configure with an unresolvable parent fails with EINVAL");
    json_decref (bad);

    ok (notify_count == 0, "no notifications fired on failed configure");
    ok (queues_lookup (qs, "batch", NULL) == batch,
        "batch still present (not freed) after failed configure");
    ok (queues_lookup (qs, "aaa", NULL) == NULL,
        "invalid queue was not added");
    vq = queues_lookup (qs, "vq", NULL);
    ok (vq != NULL && queue_parent (vq) == batch,
        "vq's parent still points at the live batch queue");
    ok (queue_root (vq) == batch, "vq's root still resolves to batch");

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

/* Look up the conf.queues entry named 'name' in array 'cq', or NULL.
 * Queue order in the array is not guaranteed, so entries are found by
 * name rather than by position.
 */
static json_t *conf_entry (json_t *cq, const char *name)
{
    size_t index;
    json_t *entry;

    json_array_foreach (cq, index, entry) {
        const char *n;
        if (json_unpack (entry, "{s:s}", "name", &n) == 0 && streq (n, name))
            return entry;
    }
    return NULL;
}

/* The queue-list response carries both the "queues" name array and a
 * "conf" object with each queue's effective config. Effective
 * 'requires'/'parent' are checked on a virtual queue.
 */
static void test_list_response (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;
    json_t *resp;
    json_t *names;
    json_t *cq;
    const char *name;
    json_t *req0;
    const char *parent2 = NULL;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* anon mode: names empty and conf.queues empty. The returned
     * reference is borrowed (owned by the cache), so it is not decref'd.
     */
    resp = queues_list_response (qs);
    ok (resp != NULL
        && json_unpack (resp,
                        "{s:o s:{s:o}}",
                        "queues", &names,
                        "conf",
                          "queues", &cq) == 0
        && json_array_size (names) == 0
        && json_array_size (cq) == 0,
        "response in anon mode: empty queues and empty conf.queues");

    /* batch (real, requires=batch), then expedite (virtual, parent
     * batch, no own requires). Queue order in the response is not
     * guaranteed, so entries are checked by name.
     */
    config = json_pack ("{s:{s:[s]} s:{s:[s]} s:{s:s}}",
                        "debug",
                          "requires", "debug",
                        "batch",
                          "requires", "batch",
                        "expedite",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    if (queues_configure (qs, config, &error) < 0)
        BAIL_OUT ("queues_configure failed: %s", error.text);
    json_decref (config);

    resp = queues_list_response (qs);
    ok (resp != NULL, "queues_list_response works in named mode");
    ok (resp
        && json_unpack (resp,
                        "{s:o s:{s:o}}",
                        "queues", &names,
                        "conf",
                          "queues", &cq) == 0,
        "response has queues array and conf.queues array");

    /* conf.queues carries an entry for each configured queue */
    ok (cq && json_array_size (cq) == 3
        && conf_entry (cq, "debug") != NULL
        && conf_entry (cq, "batch") != NULL
        && conf_entry (cq, "expedite") != NULL,
        "conf.queues has an entry for each configured queue");

    /* real queue carries its own requires, no parent key */
    ok (json_unpack (conf_entry (cq, "batch"),
                     "{s:s s:o !}",
                     "name", &name,
                     "requires", &req0) == 0
        && json_array_size (req0) == 1
        && streq (json_string_value (json_array_get (req0, 0)), "batch"),
        "real queue conf carries own requires and no parent key");

    /* virtual queue inherits parent's requires and reports parent */
    ok (json_unpack (conf_entry (cq, "expedite"),
                     "{s:s s:o s:s !}",
                     "name", &name,
                     "requires", &req0,
                     "parent", &parent2) == 0
        && streq (parent2, "batch")
        && json_array_size (req0) == 1
        && streq (json_string_value (json_array_get (req0, 0)), "batch"),
        "virtual queue conf inherits parent requires and reports parent");

    queues_destroy (qs);
}

/* ---------- virtual queue (RFC 33) tests -------------------------------- */

static void test_vqueue_configure (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *expedite;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* batch is a real queue, expedite is virtual with parent=batch.
     * Pack expedite first in the object to exercise the "parent
     * processed before the object it names" ordering case - jansson
     * preserves insertion order, so this is deterministic.
     */
    config = json_pack ("{s:{s:s} s:{}}",
                        "expedite",
                          "parent", "batch",
                        "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");

    ok (queues_configure (qs, config, &error) == 0,
        "configure with vqueue works (parent object added after child)");
    json_decref (config);

    batch = queues_lookup (qs, "batch", &error);
    ok (batch != NULL, "batch found");
    ok (!queue_is_virtual (batch), "batch is not virtual");
    ok (queue_parent (batch) == NULL, "batch has no parent");
    ok (queue_root (batch) == batch, "batch is its own root");

    expedite = queues_lookup (qs, "expedite", &error);
    ok (expedite != NULL, "expedite found");
    ok (queue_is_virtual (expedite), "expedite is virtual");
    ok (queue_parent (expedite) == batch, "expedite's parent is batch");
    ok (queue_root (expedite) == batch, "expedite's root is batch");

    /* Defense in depth: a config in which a queue names itself as parent
     * is rejected by the second-pass parent resolution (conf_policy.c
     * rejects this earlier in the normal broker path). */
    config = json_pack ("{s:{s:s}}", "selfq", "parent", "selfq");
    if (!config)
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (queues_configure (qs, config, &error) < 0 && errno == EINVAL,
        "configure with a self-parent queue fails with EINVAL");
    json_decref (config);

    queues_destroy (qs);
}

static void test_vqueue_reparent_on_reload (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *debug;
    struct queue *vq;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{} s:{s:s}}",
                        "batch",
                        "debug",
                        "vq",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "initial configure: batch, debug, vq(parent=batch)");
    json_decref (config);

    batch = queues_lookup (qs, "batch", &error);
    vq = queues_lookup (qs, "vq", &error);
    ok (batch && vq && queue_parent (vq) == batch,
        "vq initially parented to batch");

    /* Reload: reparent vq to debug */
    config = json_pack ("{s:{} s:{} s:{s:s}}",
                        "batch",
                        "debug",
                        "vq",
                          "parent", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload reparents vq to debug");
    json_decref (config);

    debug = queues_lookup (qs, "debug", &error);
    vq = queues_lookup (qs, "vq", &error);
    ok (vq != NULL, "vq still exists after reparent reload");
    ok (queue_parent (vq) == debug, "vq is now parented to debug");
    ok (queue_root (vq) == debug, "vq's root is now debug");

    /* Reload again: vq loses its parent (becomes an ordinary queue) */
    config = json_pack ("{s:{} s:{} s:{}}", "batch", "debug", "vq");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reload removes vq's parent key");
    json_decref (config);

    vq = queues_lookup (qs, "vq", &error);
    ok (vq != NULL, "vq still exists");
    ok (!queue_is_virtual (vq), "vq is no longer virtual");
    ok (queue_parent (vq) == NULL, "vq has no parent now");
    ok (queue_root (vq) == vq, "vq is now its own root");

    queues_destroy (qs);
}

static void test_vqueue_add_bad_parent (void)
{
    struct queues *qs;
    struct queue *q;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);

    /* Missing parent. This would be the FIRST named queue: a failed
     * add must leave the collection in anon mode (the add validates
     * 'parent' before any state mutates), not stranded in named mode
     * with an empty table and the anon queue destroyed.
     */
    notify_reset ();
    errno = 0;
    config = json_pack ("{s:s}", "parent", "nosuchqueue");
    if (!config)
        BAIL_OUT ("json_pack failed");
    q = queues_add (qs, "vq", config, &error);
    json_decref (config);
    ok (q == NULL && errno == EINVAL,
        "queues_add with missing parent fails cleanly with EINVAL");
    ok (queues_lookup (qs, "vq", NULL) == NULL,
        "failed add leaves no half-added queue behind");
    ok (!queues_have_named (qs)
        && queues_lookup (qs, NULL, NULL) != NULL,
        "failed first add leaves the anon queue in place");
    ok (notify_count == 0, "failed add fires no notifications");

    /* Add a real queue, then a vqueue-of-vqueue (parent is virtual) */
    ok (queues_add (qs, "batch", NULL, &error) != NULL,
        "add batch (real queue)");
    config = json_pack ("{s:s}", "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_add (qs, "expedite", config, &error) != NULL,
        "add expedite (parent=batch) works");
    json_decref (config);

    notify_reset ();
    errno = 0;
    config = json_pack ("{s:s}", "parent", "expedite");
    if (!config)
        BAIL_OUT ("json_pack failed");
    q = queues_add (qs, "subq", config, &error);
    json_decref (config);
    ok (q == NULL && errno == EINVAL,
        "queues_add with a virtual parent fails cleanly with EINVAL");
    ok (queues_lookup (qs, "subq", NULL) == NULL,
        "failed vqueue-of-vqueue add leaves no queue behind");
    ok (notify_count == 0,
        "failed vqueue-of-vqueue add fires no notifications");

    /* A queue naming itself as parent is rejected (would otherwise
     * leave q->parent == q, a queue that is its own parent). The
     * standalone add path must catch this by name, since the queue
     * being added is not in the table yet when 'parent' is validated.
     */
    errno = 0;
    config = json_pack ("{s:s}", "parent", "selfq");
    if (!config)
        BAIL_OUT ("json_pack failed");
    q = queues_add (qs, "selfq", config, &error);
    json_decref (config);
    ok (q == NULL && errno == EINVAL,
        "queues_add with self as parent fails cleanly with EINVAL");
    ok (queues_lookup (qs, "selfq", NULL) == NULL,
        "failed self-parent add leaves no queue behind");

    /* queues_update with a bad parent fails cleanly BEFORE mutating:
     * the queue's config-derived state is untouched and no "update"
     * notification fires for the rejected change.
     */
    q = queues_lookup (qs, "batch", &error);
    ok (q != NULL, "batch found for update test");
    config = json_pack ("{s:[s]}", "requires", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_update (qs, q, config, &error) == 0,
        "queues_update batch with requires works");
    json_decref (config);
    notify_reset ();
    errno = 0;
    config = json_pack ("{s:s}", "parent", "nosuchqueue");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_update (qs, q, config, &error) < 0 && errno == EINVAL,
        "queues_update with missing parent fails cleanly with EINVAL");
    json_decref (config);
    ok (queue_requires (q) != NULL
        && json_array_size (queue_requires (q)) == 1,
        "failed update leaves the queue's requires untouched");
    ok (notify_count == 0, "failed update fires no notifications");

    /* queues_update making a queue its own parent is rejected too */
    errno = 0;
    config = json_pack ("{s:s}", "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_update (qs, q, config, &error) < 0 && errno == EINVAL,
        "queues_update with self as parent fails cleanly with EINVAL");
    json_decref (config);

    queues_destroy (qs);
}

/* queues_remove() of a queue that other queues name as their parent
 * (RFC 33 virtual queues) must fail with EBUSY and an error naming the
 * dependent virtual queues, else their ->parent pointers would dangle.
 * queues_configure() is exempt: a reload may remove a parent and its
 * virtual queues together.
 */
static void test_vqueue_remove_parent_busy (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{s:s} s:{s:s}}",
                        "batch",
                        "expedite",
                          "parent", "batch",
                        "background",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch with two vqueues works");
    json_decref (config);

    errno = 0;
    error.text[0] = '\0';
    ok (queues_remove (qs, "batch", &error) < 0 && errno == EBUSY,
        "queues_remove of a parent with vqueues fails with EBUSY");
    ok (strstr (error.text, "expedite") != NULL
        && strstr (error.text, "background") != NULL,
        "error message names the dependent virtual queues");
    diag ("%s", error.text);
    ok (queues_lookup (qs, "batch", NULL) != NULL,
        "parent queue is still in the table");

    /* Removing the virtual queues first unblocks the parent */
    ok (queues_remove (qs, "expedite", &error) == 0,
        "queues_remove expedite works");
    errno = 0;
    ok (queues_remove (qs, "batch", &error) < 0 && errno == EBUSY,
        "parent removal still fails while one vqueue remains");
    ok (queues_remove (qs, "background", &error) == 0,
        "queues_remove background works");
    ok (queues_remove (qs, "batch", &error) == 0,
        "queues_remove batch works once no vqueues depend on it");

    /* A whole-table reconfigure may drop a parent and its vqueues
     * together (not subject to the EBUSY check)
     */
    config = json_pack ("{s:{} s:{s:s}}",
                        "batch",
                        "expedite",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reconfigure batch + vqueue works");
    json_decref (config);
    config = json_pack ("{s:{}}", "debug");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "reconfigure removing parent and vqueue together works");
    json_decref (config);
    ok (queues_lookup (qs, "batch", NULL) == NULL
        && queues_lookup (qs, "expedite", NULL) == NULL,
        "parent and vqueue are both gone");

    queues_destroy (qs);
}

/* queues_remove() of a parent with so many dependent virtual queues that
 * the composed EBUSY message overflows flux_error_t.text (160 bytes) must
 * still fail cleanly, and the error text must carry errprintf()'s '+'
 * truncation marker in its final byte. queues_remove() builds the list of
 * dependent vqueue names into a fixed 128-byte buffer, and errprintf()
 * marks any message it had to truncate.
 */
static void test_vqueue_remove_parent_truncated (void)
{
    struct queues *qs;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    /* One real parent 'batch' plus many virtual queues parented to it.
     * 50 four-character names comma-joined far exceeds queues_remove()'s
     * 128-byte vqueue-name buffer, so the resulting message overruns the
     * 160-byte error buffer and must be truncated.
     */
    if (!(config = json_object ()))
        BAIL_OUT ("json_object failed");
    if (json_object_set_new (config, "batch", json_object ()) < 0)
        BAIL_OUT ("json_object_set_new failed");
    for (int i = 0; i < 50; i++) {
        char name[16];
        snprintf (name, sizeof (name), "vq%02d", i);
        if (json_object_set_new (config,
                                 name,
                                 json_pack ("{s:s}", "parent", "batch")) < 0)
            BAIL_OUT ("json_object_set_new failed");
    }
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch with 50 vqueues works");
    json_decref (config);

    errno = 0;
    error.text[0] = '\0';
    ok (queues_remove (qs, "batch", &error) < 0 && errno == EBUSY,
        "queues_remove of a parent with many vqueues fails with EBUSY");
    diag ("%s", error.text);
    ok (strncmp (error.text, "queue 'batch' is the parent", 27) == 0,
        "error message identifies the parent queue");
    ok (strlen (error.text) == sizeof (error.text) - 1,
        "error message fills the error buffer (was truncated)");
    ok (error.text[sizeof (error.text) - 2] == '+',
        "truncated error message ends with the '+' truncation marker");
    ok (queues_lookup (qs, "batch", NULL) != NULL,
        "parent queue is still in the table after the failed remove");

    queues_destroy (qs);
}

static void test_vqueue_root_and_requires (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *expedite;
    flux_error_t error;
    json_t *config;
    json_t *req;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    req = json_pack ("[s]", "batch");
    if (!req)
        BAIL_OUT ("json_pack failed");
    config = json_pack ("{s:{s:O} s:{s:s}}",
                        "batch",
                          "requires", req,
                        "expedite",
                          "parent", "batch");
    json_decref (req);
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch (requires) + expedite (parent=batch)");
    json_decref (config);

    batch = queues_lookup (qs, "batch", &error);
    expedite = queues_lookup (qs, "expedite", &error);
    ok (batch && expedite, "both queues found");

    ok (queue_requires (expedite) != NULL
        && json_array_size (queue_requires (expedite)) == 1,
        "vqueue's effective requires is the parent's");
    ok (queue_requires (expedite) == queue_requires (batch),
        "vqueue's effective requires is the same object as the parent's");
    ok (queue_requires (batch) != NULL
        && json_array_size (queue_requires (batch)) == 1,
        "non-virtual queue's effective requires is its own");

    queues_destroy (qs);
}

/* Effective-started 4-state matrix: parent x vqueue, started x stopped. */
static void test_vqueue_effective_started (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *vq;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{s:s}}",
                        "batch",
                        "vq",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0, "configure batch+vq");
    json_decref (config);

    batch = queues_lookup (qs, "batch", &error);
    vq = queues_lookup (qs, "vq", &error);
    ok (batch && vq, "both queues found");

    /* Named queues start stopped: (parent stopped, vq stopped) */
    ok (!queue_is_started (batch) && !queue_is_started (vq),
        "initial: both own bits stopped");
    ok (!queue_is_started_effective (vq),
        "stopped+stopped: effective started is false");

    /* parent started, vq stopped: vq must NOT be effectively started */
    queue_start (batch, false);
    ok (queue_is_started (batch) && !queue_is_started (vq),
        "parent started, vq still stopped (own bits)");
    ok (!queue_is_started_effective (vq),
        "started(parent)+stopped(vq): effective started is false");
    ok (queue_is_started_effective (batch),
        "parent's own effective started is true (root == self)");

    /* parent stopped, vq started: vq must NOT be effectively started
     * (starting a vqueue under a stopped parent has no effect until
     * the parent starts)
     */
    queue_stop (batch, NULL, false);
    queue_start (vq, false);
    ok (!queue_is_started (batch) && queue_is_started (vq),
        "parent stopped, vq started (own bits)");
    ok (!queue_is_started_effective (vq),
        "stopped(parent)+started(vq): effective started is false");

    /* both started: vq is now effectively started */
    queue_start (batch, false);
    ok (queue_is_started (batch) && queue_is_started (vq),
        "both started (own bits)");
    ok (queue_is_started_effective (vq),
        "started+started: effective started is true");

    queues_destroy (qs);
}

/* Status encode matrix for a vqueue: all 4 (parent x vqueue) started
 * states, checking start/blocked/parent/stop_reason keys with a strict
 * unpack. Also confirms a non-virtual queue's payload is unaffected
 * (byte identical key set to pre-vqueue behavior) and that scheduler
 * offline on a started queue reports blocked.
 */
static void test_vqueue_status_encode (void)
{
    struct queues *qs;
    struct queue *batch;
    struct queue *vq;
    flux_error_t error;
    json_t *config;
    json_t *o;
    int enable, start;
    const char *blocked;
    const char *parent;
    const char *reason;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");

    config = json_pack ("{s:{} s:{s:s}}",
                        "batch",
                        "vq",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0, "configure batch+vq");
    json_decref (config);

    batch = queues_lookup (qs, "batch", &error);
    vq = queues_lookup (qs, "vq", &error);
    ok (batch && vq, "both queues found");

    /* State 1: parent stopped, vq stopped - no blocked key */
    o = queue_status_encode (vq, true);
    ok (o != NULL, "status_encode works (stopped/stopped)");
    ok (json_unpack (o,
                     "{s:b s:b s:s !}",
                     "enable", &enable,
                     "start", &start,
                     "parent", &parent) == 0
        && !start
        && streq (parent, "batch"),
        "stopped/stopped: start=false, no blocked key, parent=batch");
    json_decref (o);

    /* State 2: parent started, vq stopped - still not blocked (own
     * bit is the cause) */
    queue_start (batch, false);
    o = queue_status_encode (vq, true);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "parent", &parent) == 0
        && !start,
        "started(parent)/stopped(vq): start=false, no blocked key");
    json_decref (o);

    /* State 3: parent stopped, vq started - blocked "parent", synthesized
     * reason names the parent */
    queue_stop (batch, NULL, false);
    queue_start (vq, false);
    o = queue_status_encode (vq, true);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s s:s s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "parent", &parent,
                        "blocked", &blocked,
                        "stop_reason", &reason) == 0
        && !start
        && streq (blocked, "parent")
        && streq (reason, "parent queue 'batch' is stopped"),
        "stopped(parent)/started(vq): start=false blocked=parent with"
        " synthesized stop_reason");
    json_decref (o);

    /* State 4: both started - no blocked key */
    queue_start (batch, false);
    o = queue_status_encode (vq, true);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "parent", &parent) == 0
        && start,
        "started/started: start=true, no blocked key");
    json_decref (o);

    /* Non-virtual queue payload: unaffected, no parent/blocked keys */
    o = queue_status_encode (batch, true);
    ok (o
        && json_unpack (o,
                        "{s:b s:b !}",
                        "enable", &enable,
                        "start", &start) == 0,
        "non-virtual queue payload has exactly {enable,start} (no"
        " parent/blocked keys) - unchanged from pre-vqueue behavior");
    json_decref (o);

    /* Scheduler offline on a started queue: blocked "scheduler" with
     * synthesized reason (own state untouched) */
    o = queue_status_encode (batch, false);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "blocked", &blocked,
                        "stop_reason", &reason) == 0
        && !start
        && streq (blocked, "scheduler")
        && streq (reason, "Scheduler is offline"),
        "sched offline on started queue: start=false blocked=scheduler");
    ok (queue_is_started (batch),
        "sched offline does not modify queue state");
    json_decref (o);

    /* Scheduler offline on a started vqueue (parent started): blocked is
     * "scheduler", not "parent" - the offline condition takes precedence
     * even though the queue is virtual. */
    o = queue_status_encode (vq, false);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s s:s s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "parent", &parent,
                        "blocked", &blocked,
                        "stop_reason", &reason) == 0
        && !start
        && streq (parent, "batch")
        && streq (blocked, "scheduler")
        && streq (reason, "Scheduler is offline"),
        "sched offline on started vqueue: blocked=scheduler not parent");
    json_decref (o);

    /* Scheduler offline on a stopped queue: not blocked */
    queue_stop (batch, NULL, false);
    o = queue_status_encode (batch, false);
    ok (o
        && json_unpack (o,
                        "{s:b s:b s:s !}",
                        "enable", &enable,
                        "start", &start,
                        "stop_reason", &reason) == 0
        && !start
        && streq (reason, "Scheduler is offline"),
        "sched offline on stopped queue: no blocked key");
    json_decref (o);

    queues_destroy (qs);
}

/* Notify events for vqueues behave like ordinary queues: add/update on
 * a vqueue fires the same event names, independent of parent linkage.
 */
static void test_vqueue_notify (void)
{
    struct queues *qs;
    struct queue *vq;
    flux_error_t error;
    json_t *config;

    qs = queues_create ();
    if (!qs)
        BAIL_OUT ("queues_create failed");
    queues_set_notify (qs, notify_cb, NULL);
    notify_reset ();

    config = json_pack ("{s:{} s:{s:s}}",
                        "batch",
                        "vq",
                          "parent", "batch");
    if (!config)
        BAIL_OUT ("json_pack failed");
    ok (queues_configure (qs, config, &error) == 0,
        "configure batch+vq fires notify events");
    json_decref (config);

    bool saw_vq_add = false;
    for (int i = 0; i < notify_count; i++) {
        if (streq (notify_log[i].name, "vq")
            && streq (notify_log[i].event, "add"))
            saw_vq_add = true;
    }
    ok (saw_vq_add, "vq add event fired same as an ordinary queue");

    vq = queues_lookup (qs, "vq", &error);
    ok (vq != NULL, "vq found");

    notify_reset ();
    ok (queue_start (vq, false) == 0, "start vq");
    ok (notify_count == 1
        && streq (notify_log[0].event, "start"),
        "vq start event fired same as an ordinary queue");

    notify_reset ();
    ok (queue_stop (vq, "down", false) == 0, "stop vq");
    ok (notify_count == 1
        && streq (notify_log[0].event, "stop"),
        "vq stop event fired same as an ordinary queue");

    notify_reset ();
    ok (queue_disable (vq, "maint") == 0, "disable vq");
    ok (notify_count == 1
        && streq (notify_log[0].event, "disable"),
        "vq disable event fired same as an ordinary queue");

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
    test_configure_bad_parent_transactional ();
    test_restore_bad_entry ();
    test_admin_ops ();
    test_admin_ops_reenter ();
    test_list_names ();
    test_status_encode ();
    test_list_encode ();
    test_list_response ();

    test_vqueue_configure ();
    test_vqueue_reparent_on_reload ();
    test_vqueue_add_bad_parent ();
    test_vqueue_remove_parent_busy ();
    test_vqueue_remove_parent_truncated ();
    test_vqueue_root_and_requires ();
    test_vqueue_effective_started ();
    test_vqueue_status_encode ();
    test_vqueue_notify ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
