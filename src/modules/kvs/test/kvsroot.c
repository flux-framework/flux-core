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

int global = 0;

void basic_api_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    struct kvsroot *tmproot;
    struct flux_msg_cred cred;
    flux_msg_t *request;

    cache = cache_create (NULL);

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

    /* test convenience functions */

    ok (kvsroot_save_transaction_request (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "invalid inputs to kvsroot_save_transaction_request returns EINVAL");

    ok (zhash_size (root->transaction_requests) == 0,
        "before saving transaction, no transaction_requests in hash");

    if (!(request = flux_request_encode ("mytopic", "{ bar : 1 }")))
        BAIL_OUT ("flux_request_encode");

    ok (kvsroot_save_transaction_request (root, request, "myname") == 0,
        "kvsroot_save_transaction_request works");

    ok (kvsroot_save_transaction_request (root, request, "myname") < 0
        && errno == EEXIST,
        "kvsroot_save_transaction_request fails on duplicate request");

    flux_msg_destroy (request);

    ok (zhash_size (root->transaction_requests) == 1,
        "after saving transaction, one transaction_requests in hash");

    /* invalid input to kvsroot_setroot() won't segfault */
    kvsroot_setroot (NULL, NULL, NULL, 0);

    kvsroot_setroot (krm, root, "foobar", 18);

    ok (streq (root->ref, "foobar"),
        "kvsroot_setroot set ref correctly");

    ok (root->seq == 18,
        "kvsroot_setroot set seq correctly");

    cred.rolemask = 0;
    cred.userid = 0;
    ok (kvsroot_check_user (NULL, NULL, cred) < 0 && errno == EINVAL,
        "invalid inputs to kvsroot_check_user returns EINVAL");

    cred.rolemask = FLUX_ROLE_OWNER;
    cred.userid = 0;
    ok (kvsroot_check_user (krm, NULL, cred) < 0
        && errno == EINVAL,
        "kvsroot_check_user failed with EINVAL on bad input");

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

    /* back to testing kvsroot_mgr functions */

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

void basic_api_tests_non_primary (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;

    cache = cache_create (NULL);

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

void basic_iter_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    int count;

    cache = cache_create (NULL);

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

    cache = cache_create (NULL);

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

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api_tests ();
    basic_api_tests_non_primary ();
    basic_iter_tests ();
    basic_kvstxn_mgr_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
