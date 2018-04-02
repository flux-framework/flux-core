#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <unistd.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/modules/kvs/kvsroot.h"
#include "src/modules/kvs/treq.h"

int global = 0;

void basic_api_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    struct kvsroot *tmproot;

    cache = cache_create ();

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

    kvsroot_setroot (krm, root, "foobar", 18);

    ok (!strcmp (root->ref, "foobar"),
        "kvsroot_setroot set ref correctly");

    ok (root->seq == 18,
        "kvsroot_setroot set seq correctly");

    ok (kvsroot_check_user (krm, NULL, FLUX_ROLE_OWNER, 0) < 0
        && errno == EINVAL,
        "kvsroot_check_user failed with EINVAL on bad input");

    ok (!kvsroot_check_user (krm, root, FLUX_ROLE_OWNER, 0),
        "kvsroot_check_user works on role owner");

    ok (!kvsroot_check_user (krm, root, FLUX_ROLE_USER, 1234),
        "kvsroot_check_user works on role user and correct id");

    ok (kvsroot_check_user (krm, root, FLUX_ROLE_USER, 0) < 0
        && errno == EPERM,
        "kvsroot_check_user fails with EPERM on role user and incorrect id");

    ok (kvsroot_check_user (krm, root, 0, 0) < 0
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
    kvsroot_mgr_remove_root (krm, root->namespace);
    return 1;
}

void basic_iter_tests (void)
{
    kvsroot_mgr_t *krm;
    struct cache *cache;
    struct kvsroot *root;
    int count;

    cache = cache_create ();

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         "foo",
                                         geteuid (),
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         "bar",
                                         geteuid (),
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
        "kvsroot_mgr_iter_roots works if exitting midway");

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

    cache = cache_create ();

    ok ((krm = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         geteuid (),
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ops = json_array ();
    /* not a real operation */
    json_array_append_new (ops, json_string ("foo"));

    ok (kvstxn_mgr_add_transaction (root->ktm,
                                    "foo",
                                    ops,
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
    basic_iter_tests ();
    basic_kvstxn_mgr_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
