#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/modules/kvs/kvsroot.h"
#include "src/modules/kvs/fence.h"

int global = 0;

void basic_api_tests (void)
{
    kvsroot_mgr_t *km;
    struct cache *cache;
    struct kvsroot *root;
    struct kvsroot *tmproot;

    cache = cache_create ();

    ok ((km = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok (kvsroot_mgr_root_count (km) == 0,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((root = kvsroot_mgr_create_root (km,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (kvsroot_mgr_root_count (km) == 1,
        "kvsroot_mgr_root_count returns correct count of roots");

    ok ((tmproot = kvsroot_mgr_lookup_root (km, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root returns correct root");

    ok ((tmproot = kvsroot_mgr_lookup_root_safe (km, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root_safe works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root_safe returns correct root");

    root->remove = true;

    ok ((tmproot = kvsroot_mgr_lookup_root (km, KVS_PRIMARY_NAMESPACE)) != NULL,
        "kvsroot_mgr_lookup_root works");

    ok (tmproot == root,
        "kvsroot_mgr_lookup_root returns correct root");

    ok (kvsroot_mgr_lookup_root_safe (km, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root_safe returns NULL on root marked removed");

    ok (kvsroot_mgr_remove_root (km, KVS_PRIMARY_NAMESPACE) == 0,
        "kvsroot_mgr_remove_root works");

    ok (kvsroot_mgr_lookup_root (km, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root returns NULL after namespace removed");

    ok (kvsroot_mgr_lookup_root_safe (km, KVS_PRIMARY_NAMESPACE) == NULL,
        "kvsroot_mgr_lookup_root_safe returns NULL after namespace removed");

    kvsroot_mgr_destroy (km);

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
    kvsroot_mgr_t *km = arg;
    kvsroot_mgr_remove_root (km, root->namespace);
    return 1;
}

void basic_iter_tests (void)
{
    kvsroot_mgr_t *km;
    struct cache *cache;
    struct kvsroot *root;
    int count;

    cache = cache_create ();

    ok ((km = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (km,
                                         cache,
                                         "sha1",
                                         "foo",
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok ((root = kvsroot_mgr_create_root (km,
                                         cache,
                                         "sha1",
                                         "bar",
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (kvsroot_mgr_root_count (km) == 2,
        "kvsroot_mgr_root_count returns correct count of roots");

    count = 0;
    ok (kvsroot_mgr_iter_roots (km, count_roots_cb, &count) == 0,
        "kvsroot_mgr_iter_roots works");

    ok (count == 2,
        "kvsroot_mgr_iter_roots called callback correct number of times");

    count = 0;
    ok (kvsroot_mgr_iter_roots (km, count_roots_early_exit_cb, &count) == 0,
        "kvsroot_mgr_iter_roots works if exitting midway");

    ok (count == 1,
        "kvsroot_mgr_iter_roots called callback correct number of times");

    ok (kvsroot_mgr_iter_roots (km, roots_error_cb, NULL) < 0,
        "kvsroot_mgr_iter_roots errors on error in callback");

    ok (kvsroot_mgr_iter_roots (km, roots_remove_cb, km) == 0,
        "kvsroot_mgr_iter_roots works on remove callback");

    ok (kvsroot_mgr_root_count (km) == 1,
        "kvsroot_mgr_root_count returns correct count of roots after a removal");

    kvsroot_mgr_destroy (km);
    cache_destroy (cache);
}

void basic_commit_mgr_tests (void)
{
    kvsroot_mgr_t *km;
    struct cache *cache;
    struct kvsroot *root;
    commit_t *c;
    fence_t *f;
    json_t *ops = NULL;
    void *tmpaux;

    cache = cache_create ();

    f = fence_create ("foo", 1, 0);
    ops = json_array ();
    /* not a real operation */
    json_array_append_new (ops, json_string ("foo"));

    fence_add_request_data (f, ops);

    json_decref (ops);

    ok ((km = kvsroot_mgr_create (NULL, &global)) != NULL,
        "kvsroot_mgr_create works");

    ok ((root = kvsroot_mgr_create_root (km,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         0)) != NULL,
         "kvsroot_mgr_create_root works");

    ok (commit_mgr_add_fence (root->cm, f) == 0,
        "commit_mgr_add_fence works");

    ok (commit_mgr_process_fence_request (root->cm, "foo") == 0,
        "commit_mgr_process_fence_request works");

    ok ((c = commit_mgr_get_ready_commit (root->cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok ((tmpaux = commit_get_aux (c)) != NULL,
        "commit_get_aux returns non-NULL aux");

    ok (tmpaux == &global,
        "commit_get_aux returns correct aux value");

    kvsroot_mgr_destroy (km);
    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api_tests ();
    basic_iter_tests ();
    basic_commit_mgr_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
