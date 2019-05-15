/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/modules/kvs/kvsroot.h"
#include "src/modules/kvs/kvssync.h"
#include "src/modules/kvs/cache.h"

const char *root_ref = "1234"; /* random string, doesn't matter for tests */
int count = 0;

void basic_corner_case_tests (void)
{
    ok (kvssync_add (NULL, NULL, NULL, NULL, NULL, NULL, 0) < 0 && errno == EINVAL,
        "kvssync_add fails with EINVAL on bad input");

    ok (kvssync_remove_msg (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "kvssync_remove_msg fails with EINVAL on bad input");

    /* doesn't segfault on NULL */
    kvssync_process (NULL, false);
}

void cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    count++;
}

void basic_api_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    flux_msg_t *msg;

    cache = cache_create ();

    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL, "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0))
            != NULL,
        "kvsroot_mgr_create_root works");

    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);

    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 2),
        "kvssync_add w/ seq = 2 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 3),
        "kvssync_add w/ seq = 3 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 4),
        "kvssync_add w/ seq = 4 works");

    ok (zlist_size (root->synclist) == 3, "synclist is length 3");

    kvsroot_setroot (krm, root, root_ref, 1);

    count = 0;

    kvssync_process (root, false);

    ok (count == 0, "kvssync_process did not call cb on seq = 1");

    ok (zlist_size (root->synclist) == 3, "synclist is length 3");

    kvsroot_setroot (krm, root, root_ref, 2);

    count = 0;

    kvssync_process (root, false);

    ok (count == 1, "kvssync_process called callback once on seq = 2");

    ok (zlist_size (root->synclist) == 2, "synclist is length 2");

    kvsroot_setroot (krm, root, root_ref, 4);

    count = 0;

    kvssync_process (root, false);

    ok (count == 2, "kvssync_process called callback twice on seq = 4");

    ok (zlist_size (root->synclist) == 0, "synclist is length 0");

    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 5),
        "kvssync_add w/ seq = 5 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 6),
        "kvssync_add w/ seq = 6 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 7),
        "kvssync_add w/ seq = 7 works");

    ok (zlist_size (root->synclist) == 3, "synclist is length 3");

    count = 0;

    kvssync_process (root, true);

    ok (count == 3, "kvssync_process called callback thrice on all flag = true");

    ok (zlist_size (root->synclist) == 0, "synclist is length 0");

    /* cover some alternate insertion pattern, descending and
     * duplicate numbers */

    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 9),
        "kvssync_add w/ seq = 9 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 8),
        "kvssync_add w/ seq = 8 works");
    ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, 8),
        "kvssync_add w/ seq = 8 works");

    ok (zlist_size (root->synclist) == 3, "synclist is length 3");

    count = 0;

    kvssync_process (root, true);

    ok (count == 3, "kvssync_process called callback thrice on all flag = true");

    flux_msg_destroy (msg);

    kvsroot_mgr_destroy (krm);

    cache_destroy (cache);
}

bool msgcmp (const flux_msg_t *msg, void *arg)
{
    char *id = NULL;
    bool match = false;
    if (flux_msg_get_route_first (msg, &id) == 0
        && (!strcmp (id, "1") || !strcmp (id, "2") || !strcmp (id, "3")
            || !strcmp (id, "4") || !strcmp (id, "5")))
        match = true;
    if (id)
        free (id);
    return match;
}

bool msgcmp_true (const flux_msg_t *msg, void *arg)
{
    return true;
}

void basic_remove_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    int i;

    cache = cache_create ();

    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL, "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0))
            != NULL,
        "kvsroot_mgr_create_root works");

    /* Add 10 syncs to queue, selectively destroy */
    for (i = 1; i <= 10; i++) {
        flux_msg_t *msg;
        char s[16];
        snprintf (s, sizeof (s), "%d", i);
        if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
            break;
        if (flux_msg_enable_route (msg) < 0 || flux_msg_push_route (msg, s) < 0)
            break;
        ok (!kvssync_add (root, cb, NULL, NULL, msg, NULL, i),
            "kvssync_add w/ seq = %d works",
            i);
        flux_msg_destroy (msg);
    }

    ok (zlist_size (root->synclist) == 10, "synclist is length 10");

    count = 0;

    ok (!kvssync_remove_msg (root, msgcmp, NULL), "kvssync_remove_msg works");

    ok (zlist_size (root->synclist) == 5, "synclist is length 5");

    ok (!kvssync_remove_msg (root, msgcmp, NULL), "kvssync_remove_msg works");

    ok (zlist_size (root->synclist) == 5, "synclist is still length 5");

    ok (!kvssync_remove_msg (root, msgcmp_true, NULL), "kvssync_remove_msg works");

    ok (zlist_size (root->synclist) == 0, "synclist is length 0");

    kvsroot_mgr_destroy (krm);

    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_corner_case_tests ();
    basic_api_tests ();
    basic_remove_tests ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
