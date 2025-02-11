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
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libkvs/kvs.h"
#include "src/modules/kvs/kvsroot.h"
#include "ccan/str/str.h"

const char *root_ref = "1234";  /* random string, doesn't matter for tests */
int global = 0;

void basic_kvsroot_mgr_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    struct kvsroot *tmproot;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (root->is_primary == true,
        "root is primary namespace");

    ok (kvsroot_mgr_root_count (krm) == 1,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((tmproot = kvsroot_mgr_lookup_root (krm, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root returns correct root");

    ok ((tmproot = kvsroot_mgr_lookup_root_safe (krm, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root_safe works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root_safe returns correct root");

    root->remove = true;

    ok ((tmproot = kvsroot_mgr_lookup_root (krm, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root returns correct root");

    ok (kvsroot_mgr_lookup_root_safe (krm, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root_safe returns NULL on root marked removed");

    ok (kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE) == 0,
        "kvsroot_mgr_remove_root works");

    ok (kvsroot_mgr_lookup_root (krm, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root returns NULL after namespace removed");

    ok (kvsroot_mgr_lookup_root_safe (krm, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root_safe returns NULL after namespace removed");

    kvsroot_mgr_destroy (krm);

    /* destroy works with NULL */
    kvsroot_mgr_destroy (NULL);

    cache_destroy (cache);
}

void basic_kvsroot_mgr_tests_non_primary (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         "foobar",
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (root->is_primary == false,
        "root is not primary namespace");

    kvsroot_mgr_destroy (krm);

    cache_destroy (cache);
}

int count_roots_cb (struct kvsroot *root, void *arg)
{
    int *count = arg;
    (*count)++;
    return 0;
}

int count_roots_early_exit_cb (struct kvsroot *root, void *arg)
{
    int *count = arg;
    (*count)++;
    return 1;
}

int roots_error_cb (struct kvsroot *root, void *arg)
{
    return -1;
}


int roots_remove_cb (struct kvsroot *root, void *arg)
{
    kvsroot_mgr_t *krm = arg;
    kvsroot_mgr_remove_root (krm, root->ns_name);
    return 1;
}

void basic_kvsroot_mgr_iter_roots (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    int count;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         "foo",
                                         getuid (),
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         "bar",
                                         getuid (),
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (kvsroot_mgr_root_count (krm) == 2,
        "kvsroot_mgr_root_count returns correct count of roots");

    count = 0;
    ok (kvsroot_mgr_iter_roots (krm, count_roots_cb, &count) == 0,
        "kvsroot_mgr_iter_roots works");

    ok (count == 2,
        "kvsroot_mgr_iter_roots called callback correct number of times");

    count = 0;
    ok (kvsroot_mgr_iter_roots (krm, count_roots_early_exit_cb, &count) == 0,
        "kvsroot_mgr_iter_roots works if exiting midway");

    ok (count == 1,
        "kvsroot_mgr_iter_roots called callback correct number of times");

    ok (kvsroot_mgr_iter_roots (krm, roots_error_cb, NULL) < 0,
        "kvsroot_mgr_iter_roots errors on error in callback");

    ok (kvsroot_mgr_iter_roots (krm, roots_remove_cb, krm) == 0,
        "kvsroot_mgr_iter_roots works on remove callback");

    ok (kvsroot_mgr_root_count (krm) == 1,
        "kvsroot_mgr_root_count returns correct count of roots after a removal");

    kvsroot_mgr_destroy (krm);
    cache_destroy (cache);
}

void basic_kvstxn_mgr_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    kvstxn_t *kt;
    json_t *ops = NULL;
    void *tmpaux;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         getuid (),
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ops = json_pack ("[{s:s s:i s:n}]",
                     "key", "a.b.c",
                     "flags", 0,
                     "dirent");

    ok (kvstxn_mgr_add_transaction (root->ktm,
                                    "foo",
                                    ops,
                                    0,
                                    0) == 0,
        "kvstxn_mgr_add_transaction works");

    json_decref (ops);

    ok ((kt = kvstxn_mgr_get_ready_transaction (root->ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok ((tmpaux = kvstxn_get_aux (kt)) != NULL,
        "kvstxn_get_aux returns non-NULL aux");

    ok (tmpaux == &global,
        "kvstxn_get_aux returns correct aux value");

    kvsroot_mgr_destroy (krm);
    cache_destroy (cache);
}

void basic_convenience_corner_case_tests (void)
{
    kvsroot_mgr_t *krm;
    struct flux_msg_cred cred;

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_save_transaction_request (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "invalid inputs to kvsroot_save_transaction_request returns EINVAL");

    /* invalid input to kvsroot_setroot() won't segfault */
    kvsroot_setroot (NULL, NULL, NULL, 0);

    cred.rolemask = FLUX_ROLE_OWNER;
    cred.userid = 0;
    ok (kvsroot_check_user (krm, NULL, cred) < 0
        && errno == EINVAL,
        "kvsroot_check_user failed with EINVAL on bad input");

    ok (kvs_wait_version_add (NULL, NULL, NULL, NULL, NULL, NULL, 0) < 0
        && errno == EINVAL,
        "kvs_wait_version_add fails with EINVAL on bad input");

    ok (kvs_wait_version_remove_msg (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "kvs_wait_version_remove_msg fails with EINVAL on bad input");

    /* doesn't segfault on NULL */
    kvs_wait_version_process (NULL, false);

    kvsroot_mgr_destroy (krm);
}

void basic_transaction_request_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    flux_msg_t *request;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (zhashx_size (root->transaction_requests) == 0,
        "before saving transaction, no transaction_requests in hash");

    if (!(request = flux_request_encode ("mytopic", "{ bar : 1 }")))
        BAIL_OUT ("flux_request_encode");

    ok (kvsroot_save_transaction_request (root, request, "myname") == 0,
        "kvsroot_save_transaction_request works");

    ok (kvsroot_save_transaction_request (root, request, "myname") < 0
        && errno == EEXIST,
        "kvsroot_save_transaction_request fails on duplicate request");

    flux_msg_destroy (request);

    ok (zhashx_size (root->transaction_requests) == 1,
        "after saving transaction, one transaction_requests in hash");

    kvsroot_mgr_destroy (krm);
    cache_destroy (cache);
}

void basic_setroot_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    kvsroot_setroot (krm, root, "foobar", 18);

    ok (streq (root->ref, "foobar"),
        "kvsroot_setroot set ref correctly");

    ok (root->seq == 18,
        "kvsroot_setroot set seq correctly");

    kvsroot_mgr_destroy (krm);
    cache_destroy (cache);
}

void basic_check_user_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    struct flux_msg_cred cred;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    cred.rolemask = 0;
    cred.userid = 0;
    ok (kvsroot_check_user (NULL, NULL, cred) < 0 && errno == EINVAL,
        "invalid inputs to kvsroot_check_user returns EINVAL");

    cred.rolemask = FLUX_ROLE_OWNER;
    cred.userid = 0;
    ok (!kvsroot_check_user (krm, root, cred),
        "kvsroot_check_user works on role owner");

    cred.rolemask = FLUX_ROLE_OWNER;
    cred.userid = 1234;
    ok (!kvsroot_check_user (krm, root, cred),
        "kvsroot_check_user works on role user and correct id");

    cred.rolemask = FLUX_ROLE_USER;
    cred.userid = 0;
    ok (kvsroot_check_user (krm, root, cred) < 0
        && errno == EPERM,
        "kvsroot_check_user fails with EPERM on role user and incorrect id");

    cred.rolemask = 0;
    cred.userid = 0;
    ok (kvsroot_check_user (krm, root, cred) < 0
        && errno == EPERM,
        "kvsroot_check_user fails with EPERM on bad role");

    kvsroot_mgr_destroy (krm);
    cache_destroy (cache);
}

void wait_version_cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    int *count = arg;
    (*count)++;
}

void basic_wait_version_add_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    flux_msg_t *msg;
    int count = 0;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);

    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 2),
        "kvs_wait_version_add w/ seq = 2 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 3),
        "kvs_wait_version_add w/ seq = 3 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 4),
        "kvs_wait_version_add w/ seq = 4 works");

    ok (zlist_size (root->wait_version_list) == 3,
        "wait_version_list is length 3");

    kvsroot_setroot (krm, root, root_ref, 1);

    count = 0;

    kvs_wait_version_process (root, false);

    ok (count == 0,
        "kvs_wait_version_process did not call cb on seq = 1");

    ok (zlist_size (root->wait_version_list) == 3,
        "wait_version_list is length 3");

    kvsroot_setroot (krm, root, root_ref, 2);

    count = 0;

    kvs_wait_version_process (root, false);

    ok (count == 1,
        "kvs_wait_version_process called callback once on seq = 2");

    ok (zlist_size (root->wait_version_list) == 2,
        "wait_version_list is length 2");

    kvsroot_setroot (krm, root, root_ref, 4);

    count = 0;

    kvs_wait_version_process (root, false);

    ok (count == 2,
        "kvs_wait_version_process called callback twice on seq = 4");

    ok (zlist_size (root->wait_version_list) == 0,
        "wait_version_list is length 0");

    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 5),
        "kvs_wait_version_add w/ seq = 5 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 6),
        "kvs_wait_version_add w/ seq = 6 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 7),
        "kvs_wait_version_add w/ seq = 7 works");

    ok (zlist_size (root->wait_version_list) == 3,
        "wait_version_list is length 3");

    count = 0;

    kvs_wait_version_process (root, true);

    ok (count == 3,
        "kvs_wait_version_process called callback thrice on all flag = true");

    ok (zlist_size (root->wait_version_list) == 0,
        "wait_version_list is length 0");

    /* cover some alternate insertion pattern, descending and
     * duplicate numbers */

    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 9),
        "kvs_wait_version_add w/ seq = 9 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 8),
        "kvs_wait_version_add w/ seq = 8 works");
    ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, &count, 8),
        "kvs_wait_version_add w/ seq = 8 works");

    ok (zlist_size (root->wait_version_list) == 3,
        "wait_version_list is length 3");

    count = 0;

    kvs_wait_version_process (root, true);

    ok (count == 3,
        "kvs_wait_version_process called callback thrice on all flag = true");

    flux_msg_destroy (msg);

    kvsroot_mgr_destroy (krm);

    cache_destroy (cache);
}

bool msgcmp (const flux_msg_t *msg, void *arg)
{
    const char *id;
    bool match = false;
    if ((id = flux_msg_route_first (msg))
        && (streq (id, "1")
            || streq (id, "2")
            || streq (id, "3")
            || streq (id, "4")
            || streq (id, "5")))
        match = true;
    return match;
}

bool msgcmp_true (const flux_msg_t *msg, void *arg)
{
    return true;
}

void basic_wait_version_remove_msg_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    int i;

    if (!(cache = cache_create (NULL)))
        BAIL_OUT ("cache_create");

    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (krm) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         1234,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");


    /* Add 10 syncs to queue, selectively destroy */
    for (i = 1; i <= 10; i++) {
        flux_msg_t *msg;
        char s[16];
        snprintf (s, sizeof (s), "%d", i);
        if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
            break;
        flux_msg_route_enable (msg);
        if (flux_msg_route_push (msg, s) < 0)
            break;
        ok (!kvs_wait_version_add (root, wait_version_cb, NULL, NULL, msg, NULL, i),
            "kvs_wait_version_add w/ seq = %d works", i);
        flux_msg_destroy (msg);
    }

    ok (zlist_size (root->wait_version_list) == 10,
        "wait_version_list is length 10");

    ok (!kvs_wait_version_remove_msg (root, msgcmp, NULL),
        "kvs_wait_version_remove_msg works");

    ok (zlist_size (root->wait_version_list) == 5,
        "wait_version_list is length 5");

    ok (!kvs_wait_version_remove_msg (root, msgcmp, NULL),
        "kvs_wait_version_remove_msg works");

    ok (zlist_size (root->wait_version_list) == 5,
        "wait_version_list is still length 5");

    ok (!kvs_wait_version_remove_msg (root, msgcmp_true, NULL),
        "kvs_wait_version_remove_msg works");

    ok (zlist_size (root->wait_version_list) == 0,
        "wait_version_list is length 0");

    kvsroot_mgr_destroy (krm);

    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_kvsroot_mgr_tests ();
    basic_kvsroot_mgr_tests_non_primary ();
    basic_kvsroot_mgr_iter_roots ();
    basic_kvstxn_mgr_tests ();
    basic_convenience_corner_case_tests ();
    basic_transaction_request_tests ();
    basic_setroot_tests ();
    basic_check_user_tests ();
    basic_wait_version_add_tests ();
    basic_wait_version_remove_msg_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
