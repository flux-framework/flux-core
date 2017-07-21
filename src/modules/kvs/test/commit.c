#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/common/libkvs/jansson_dirent.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/commit.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/fence.h"
#include "src/modules/kvs/kvs_util.h"
#include "src/modules/kvs/types.h"

static int test_global = 5;

/* Append a JSON object containing
 *     { "key" : key, "dirent" : dirent }
 *     { "key" : key, "dirent" : null }
 * to a json array.
 */
void ops_append (json_t *array, const char *key, const char *value)
{
    json_t *op;
    json_t *o;

    op = json_object ();
    o = json_string (key);
    json_object_set_new (op, "key", o);

    if (value) {
        json_t *dirent = j_dirent_create ("FILEVAL", json_string (value));
        json_object_set_new (op, "dirent", dirent);
    }
    else {
        json_t *null;
        null = json_null ();
        json_object_set_new (op, "dirent", null);
    }
    json_array_append (array, op);
}

struct cache *create_cache_with_empty_rootdir (href_t ref)
{
    struct cache *cache;
    struct cache_entry *hp;
    json_t *rootdir = json_object ();

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (kvs_util_json_hash ("sha1", rootdir, ref) == 0,
        "kvs_util_json_hash worked");
    ok ((hp = cache_entry_create (rootdir)) != NULL,
        "cache_entry_create works");
    cache_insert (cache, ref, hp);
    return cache;
}

void commit_mgr_basic_tests (void)
{
    struct cache *cache;
    json_t *ops = NULL;
    commit_mgr_t *cm;
    commit_t *c;
    fence_t *f, *tf;
    href_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    ok (commit_mgr_get_noop_stores (cm) == 0,
        "commit_mgr_get_noop_stores works");

    commit_mgr_clear_noop_stores (cm);

    ok ((f = fence_create ("fence1", 1, 0)) != NULL,
        "fence_create works");

    ok (commit_mgr_add_fence (cm, f) == 0,
        "commit_mgr_add_fence works");

    ok (commit_mgr_add_fence (cm, f) < 0,
        "commit_mgr_add_fence fails on duplicate fence");

    ok ((tf = commit_mgr_lookup_fence (cm, "fence1")) != NULL,
        "commit_mgr_lookup_fence works");

    ok (f == tf,
        "commit_mgr_lookup_fence returns correct fence");

    ok (commit_mgr_lookup_fence (cm, "invalid") == NULL,
        "commit_mgr_lookup_fence can't find invalid fence");

    ok (commit_mgr_process_fence_request (cm, f) == 0,
        "commit_mgr_process_fence_request works");

    ok (commit_mgr_commits_ready (cm) == false,
        "commit_mgr_commits_ready says no fences are ready");

    ok (commit_mgr_get_ready_commit (cm) == NULL,
        "commit_mgr_get_ready_commit returns NULL for no ready commits");

    ops = json_array ();
    ops_append (ops, "key1", "1");
 
    ok (fence_add_request_data (f, ops) == 0,
        "fence_add_request_data add works");

    json_decref (ops);

    ok (commit_mgr_process_fence_request (cm, f) == 0,
        "commit_mgr_process_fence_request works");

    ok (commit_mgr_commits_ready (cm) == true,
        "commit_mgr_commits_ready says a fence is ready");

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns != NULL for ready commits");

    commit_mgr_remove_commit (cm, c);

    ok (commit_mgr_commits_ready (cm) == false,
        "commit_mgr_commits_ready says no fences are ready");

    ok (commit_mgr_get_ready_commit (cm) == NULL,
        "commit_mgr_get_ready_commit returns NULL no ready commits");

    commit_mgr_remove_fence (cm, "fence1");

    ok (commit_mgr_lookup_fence (cm, "fence1") == NULL,
        "commit_mgr_lookup_fence can't find removed fence");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void create_ready_commit (commit_mgr_t *cm,
                          const char *name,
                          const char *key,
                          const char *val,
                          int flags)
{
    fence_t *f;
    json_t *ops = NULL;

    ok ((f = fence_create (name, 1, flags)) != NULL,
        "fence_create works");

    ops = json_array ();
    ops_append (ops, key, val);

    ok (fence_add_request_data (f, ops) == 0,
        "fence_add_request_data add works");

    json_decref (ops);

    ok (commit_mgr_add_fence (cm, f) == 0,
        "commit_mgr_add_fence works");

    ok (commit_mgr_process_fence_request (cm, f) == 0,
        "commit_mgr_process_fence_request works");

    ok (commit_mgr_commits_ready (cm) == true,
        "commit_mgr_commits_ready says a commit is ready");
}

void verify_ready_commit (commit_mgr_t *cm,
                          json_t *names,
                          json_t *ops,
                          const char *extramsg)
{
    json_t *o;
    commit_t *c;
    fence_t *f;

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok ((f = commit_get_fence (c)) != NULL,
        "commit_get_fence returns commit fence");

    ok ((o = fence_get_json_names (f)) != NULL,
        "fence_get_json_names works");

    ok (json_equal (names, o) == true,
        "names match %s", extramsg);

    ok ((o = fence_get_json_ops (f)) != NULL,
        "fence_get_json_ops works");

    ok (json_equal (ops, o) == true,
        "ops match %s", extramsg);
}

void clear_ready_commits (commit_mgr_t *cm)
{
    commit_t *c;

    while ((c = commit_mgr_get_ready_commit (cm)))
        commit_mgr_remove_commit (cm, c);
}

void commit_mgr_merge_tests (void)
{
    struct cache *cache;
    json_t *names, *ops = NULL;
    commit_mgr_t *cm;
    href_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* test successful merge */

    create_ready_commit (cm, "fence1", "key1", "1", 0);
    create_ready_commit (cm, "fence2", "key2", "2", 0);

    commit_mgr_merge_ready_commits (cm);

    names = json_array ();
    json_array_append (names, json_string ("fence1"));
    json_array_append (names, json_string ("fence2"));

    ops = json_array ();
    ops_append (ops, "key1", "1");
    ops_append (ops, "key2", "2");

    verify_ready_commit (cm, names, ops, "merged fence");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_commits (cm);

    commit_mgr_remove_fence (cm, "fence1");
    commit_mgr_remove_fence (cm, "fence2");

    /* test unsuccessful merge */

    create_ready_commit (cm, "fence1", "key1", "1", FLUX_KVS_NO_MERGE);
    create_ready_commit (cm, "fence2", "key2", "2", 0);

    commit_mgr_merge_ready_commits (cm);

    names = json_array ();
    json_array_append (names, json_string ("fence1"));

    ops = json_array ();
    ops_append (ops, "key1", "1");

    verify_ready_commit (cm, names, ops, "unmerged fence");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_commits (cm);

    commit_mgr_remove_fence (cm, "fence1");
    commit_mgr_remove_fence (cm, "fence2");

    /* test unsuccessful merge */

    create_ready_commit (cm, "fence1", "key1", "1", 0);
    create_ready_commit (cm, "fence2", "key2", "2", FLUX_KVS_NO_MERGE);

    commit_mgr_merge_ready_commits (cm);

    names = json_array ();
    json_array_append (names, json_string ("fence1"));

    ops = json_array ();
    ops_append (ops, "key1", "1");

    verify_ready_commit (cm, names, ops, "unmerged fence");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_commits (cm);

    commit_mgr_remove_fence (cm, "fence1");
    commit_mgr_remove_fence (cm, "fence2");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

int ref_noop_cb (commit_t *c, const char *ref, void *data)
{
    return 0;
}

int cache_noop_cb (commit_t *c, struct cache_entry *hp, void *data)
{
    return 0;
}

void commit_basic_tests (void)
{
    struct cache *cache;
    json_t *names, *ops = NULL;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "key1", "1", 0);

    names = json_array ();
    json_array_append (names, json_string ("fence1"));

    ops = json_array ();
    ops_append (ops, "key1", "1");

    verify_ready_commit (cm, names, ops, "basic test");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_get_errnum (c) == 0,
        "commit_get_errnum returns no error");

    ok (commit_get_aux_errnum (c) == 0,
        "commit_get_aux_errnum returns no error");

    ok (commit_set_aux_errnum (c, EINVAL) == EINVAL,
        "commit_set_aux_errnum works");

    ok (commit_get_aux_errnum (c) == EINVAL,
        "commit_get_aux_errnum gets EINVAL");

    ok (commit_get_errnum (c) == 0,
        "commit_get_errnum still works");

    ok (commit_get_aux (c) == &test_global,
        "commit_get_aux returns correct pointer");

    ok (commit_get_newroot_ref (c) == NULL,
        "commit_get_newroot_ref returns NULL when processing not complete");

    ok (commit_iter_missing_refs (c, ref_noop_cb, NULL) < 0,
        "commit_iter_missing_refs returns < 0 for call on invalid state");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) < 0,
        "commit_iter_dirty_cache_entries returns < 0 for call on invalid state");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

int cache_count_cb (commit_t *c, struct cache_entry *hp, void *data)
{
    int *count = data;
    if (cache_entry_get_dirty (hp)) {
        if (count)
            (*count)++;
    }
    return 0;
}

void verify_value (struct cache *cache,
                   const char *root_ref,
                   const char *key,
                   const char *val)
{
    lookup_t *lh;
    json_t *test, *o;

    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             key,
                             0)) != NULL,
        "lookup_create key %s", key);

    ok (lookup (lh) == true,
        "lookup found result");

    if (val) {
        test = json_string (val);
        ok ((o = lookup_get_value (lh)) != NULL,
            "lookup_get_value returns non-NULL as expected");
        ok (json_equal (test, o) == true,
            "lookup_get_value returned matching value");
        json_decref (test);
    }
    else
        ok (lookup_get_value (lh) == NULL,
            "lookup_get_value returns NULL as expected");

    lookup_destroy (lh);
}

void commit_basic_commit_process_test (void)
{
    struct cache *cache;
    int count = 0;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "key1", "1", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_count_cb, &count) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");

    commit_mgr_remove_commit (cm, c);

    ok ((c = commit_mgr_get_ready_commit (cm)) == NULL,
        "commit_mgr_get_ready_commit returns NULL, no more commits");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_basic_commit_process_test_multiple_fences (void)
{
    struct cache *cache;
    int count = 0;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "key1", "1", 0);
    create_ready_commit (cm, "fence2", "key2", "2", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_count_cb, &count) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    strcpy (rootref, newroot);

    /* get rid of the this commit, we're done */
    commit_mgr_remove_commit (cm, c);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    count = 0;

    ok (commit_iter_dirty_cache_entries (c, cache_count_cb, &count) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");
    verify_value (cache, newroot, "key2", "2");

    commit_mgr_remove_commit (cm, c);

    ok ((c = commit_mgr_get_ready_commit (cm)) == NULL,
        "commit_mgr_get_ready_commit returns NULL, no more commits");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_basic_commit_process_test_multiple_fences_merge (void)
{
    struct cache *cache;
    int count = 0;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "foo.key1", "1", 0);
    create_ready_commit (cm, "fence2", "bar.key2", "2", 0);

    /* merge ready commits */
    commit_mgr_merge_ready_commits (cm);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_count_cb, &count) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    /* why three? 1 for root, 1 for foo.key1 (a new dir), and 1 for
     * bar.key2 (a new dir)
     */

    ok (count == 3,
        "correct number of cache entries were dirty");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "foo.key1", "1");
    verify_value (cache, newroot, "bar.key2", "2");

    commit_mgr_remove_commit (cm, c);

    ok ((c = commit_mgr_get_ready_commit (cm)) == NULL,
        "commit_mgr_get_ready_commit returns NULL, no more commits");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

struct rootref_data {
    struct cache *cache;
    const char *rootref;
};

int rootref_cb (commit_t *c, const char *ref, void *data)
{
    struct rootref_data *rd = data;
    json_t *rootdir = json_object ();
    struct cache_entry *hp;

    ok (strcmp (ref, rd->rootref) == 0,
        "missing root reference is what we expect it to be");

    ok ((hp = cache_entry_create (rootdir)) != NULL,
        "cache_entry_create works");

    cache_insert (rd->cache, ref, hp);

    return 0;
}

void commit_process_root_missing (void)
{
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;
    struct rootref_data rd;
    json_t *rootdir = json_object ();
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok (kvs_util_json_hash ("sha1", rootdir, rootref) == 0,
        "kvs_util_json_hash worked");

    json_decref (rootdir);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "key1", "1", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_LOAD_MISSING_REFS,
        "commit_process returns COMMIT_PROCESS_LOAD_MISSING_REFS");

    /* user forgot to call commit_iter_missing_refs() test */
    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_LOAD_MISSING_REFS,
        "commit_process returns COMMIT_PROCESS_LOAD_MISSING_REFS again");

    rd.cache = cache;
    rd.rootref = rootref;

    ok (commit_iter_missing_refs (c, rootref_cb, &rd) == 0,
        "commit_iter_missing_refs works for dirty cache entries");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    /* user forgot to call commit_iter_dirty_cache_entries() test */
    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES again");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

struct missingref_data {
    struct cache *cache;
    const char *dir_ref;
    json_t *dir;
};

int missingref_cb (commit_t *c, const char *ref, void *data)
{
    struct missingref_data *md = data;
    struct cache_entry *hp;

    ok (strcmp (ref, md->dir_ref) == 0,
        "missing reference is what we expect it to be");

    ok ((hp = cache_entry_create (md->dir)) != NULL,
        "cache_entry_create works");

    cache_insert (md->cache, ref, hp);

    return 0;
}

void commit_process_missing_ref (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    struct missingref_data md;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "dir.fileval", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_LOAD_MISSING_REFS,
        "commit_process returns COMMIT_PROCESS_LOAD_MISSING_REFS");

    /* user forgot to call commit_iter_missing_refs() test */
    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_LOAD_MISSING_REFS,
        "commit_process returns COMMIT_PROCESS_LOAD_MISSING_REFS again");

    md.cache = cache;
    md.dir_ref = dir_ref;
    md.dir = dir;

    ok (commit_iter_missing_refs (c, missingref_cb, &md) == 0,
        "commit_iter_missing_refs works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    /* user forgot to call commit_iter_dirty_cache_entries() test */
    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES again");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.fileval", "52");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

int ref_error_cb (commit_t *c, const char *ref, void *data)
{
    return -1;
}

int cache_error_cb (commit_t *c, struct cache_entry *hp, void *data)
{
    return -1;
}

void commit_process_error_callbacks (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "dir.file", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_LOAD_MISSING_REFS,
        "commit_process returns COMMIT_PROCESS_LOAD_MISSING_REFS");

    ok (commit_iter_missing_refs (c, ref_error_cb, NULL) < 0,
        "commit_iter_missing_refs errors on callback error");

    /* insert cache entry now, want don't want missing refs on next
     * commit_process call */
    cache_insert (cache, dir_ref, cache_entry_create (dir));

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_error_cb, NULL) < 0,
        "commit_iter_dirty_cache_entries errors on callback error");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

struct error_partway_data {
    int total_calls;
    int success_returns;
};

int cache_error_partway_cb (commit_t *c, struct cache_entry *hp, void *data)
{
    struct error_partway_data *epd = data;
    epd->total_calls++;
    if (epd->total_calls > 1)
        return -1;
    epd->success_returns++;
    return 0;
}

void commit_process_error_callbacks_partway (void) {
    struct cache *cache;
    struct error_partway_data epd = { .total_calls = 0, .success_returns = 0};
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "dir.fileA", "52", 0);
    create_ready_commit (cm, "fence2", "dir.fileB", "53", 0);

    /* merge these commits */
    commit_mgr_merge_ready_commits (cm);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_error_partway_cb, &epd) < 0,
        "commit_iter_dirty_cache_entries errors on callback error");

    ok (epd.total_calls == 2,
        "correct number of total calls to dirty cache callback");
    ok (epd.success_returns == 1,
        "correct number of successful returns from dirty cache callback");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_invalid_operation (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", ".", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_ERROR,
        "commit_process returns COMMIT_PROCESS_ERROR");

    /* error is caught continuously */
    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_ERROR,
        "commit_process returns COMMIT_PROCESS_ERROR again");

    ok (commit_get_errnum (c) == EINVAL,
        "commit_get_errnum return EINVAL");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_invalid_hash (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "foobar", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "dir.fileval", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_ERROR,
        "commit_process returns COMMIT_PROCESS_ERROR");

    /* verify commit_process() does not continue processing */
    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_ERROR,
        "commit_process returns COMMIT_PROCESS_ERROR on second call");

    ok (commit_get_errnum (c) == EINVAL,
        "commit_get_errnum return EINVAL %d", commit_get_errnum (c));

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_follow_link (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> }
     *   "linkval" : { "LINKVAL" : "dir" } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));
    json_object_set (root,
                     "linkval",
                     j_dirent_create ("LINKVAL", json_string ("dir")));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "linkval.fileval", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "linkval.fileval", "52");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_dirval_test (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dirval" : { "DIRVAL" : { "fileval" : { "FILEVAL" : "42" } } } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    root = json_object ();
    json_object_set (root, "dirval", j_dirent_create ("DIRVAL", dir));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "dirval.fileval", "52", 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dirval.fileval", "52");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_delete_test (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* NULL value --> delete */
    create_ready_commit (cm, "fence1", "dir.fileval", NULL, 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.fileval", NULL);

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_delete_nosubdir_test (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* subdir doesn't exist for this key */
    /* NULL value --> delete */
    create_ready_commit (cm, "fence1", "noexistdir.fileval", NULL, 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "noexistdir.fileval", NULL);

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_delete_filevalinpath_test (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* fileval is in path */
    /* NULL value --> delete */
    create_ready_commit (cm, "fence1", "dir.fileval.filebaz", NULL, 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.fileval.filebaz", NULL);

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

void commit_process_big_fileval (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;
    int bigstrsize = BLOBREF_MAX_STRING_SIZE * 2;
    char bigstr[bigstrsize];
    int i;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * dir
     * { "fileval" : { "FILEVAL" : "42" } }
     *
     */

    dir = json_object();
    json_object_set (dir,
                     "fileval",
                     j_dirent_create ("FILEVAL", json_string ("42")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    memset (bigstr, '\0', bigstrsize);
    for (i = 0; i < bigstrsize - 1; i++)
        bigstr[i] = 'a';

    create_ready_commit (cm, "fence1", "dir.fileval", bigstr, 0);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.fileval", bigstr);

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

/* Test giant directory entry, as large json objects will iterate through
 * their entries randomly based on the internal hash data structure.
 */
void commit_process_giant_dir (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_t *root;
    json_t *dir;
    href_t root_ref;
    href_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is.
     *
     * root
     * { "dir" : { "DIRREF" : <ref to dir> } }
     *
     * Mix up keys and upper/lower case to get different hash ordering
     * other than the "obvious" one.
     *
     * dir
     * { "fileval0000" : { "FILEVAL" : "0" },
     *   "fileval0010" : { "FILEVAL" : "1" },
     *   "fileval0200" : { "FILEVAL" : "2" },
     *   "fileval3000" : { "FILEVAL" : "3" },
     *   "fileval0004" : { "FILEVAL" : "4" },
     *   "fileval0050" : { "FILEVAL" : "5" },
     *   "fileval0600" : { "FILEVAL" : "6" },
     *   "fileval7000" : { "FILEVAL" : "7" },
     *   "fileval0008" : { "FILEVAL" : "8" },
     *   "fileval0090" : { "FILEVAL" : "9" },
     *   "fileval0a00" : { "FILEVAL" : "A" },
     *   "filevalB000" : { "FILEVAL" : "b" },
     *   "fileval000c" : { "FILEVAL" : "C" },
     *   "fileval00D0" : { "FILEVAL" : "d" },
     *   "fileval0e00" : { "FILEVAL" : "E" },
     *   "filevalF000" : { "FILEVAL" : "f" } }
     *
     */

    dir = json_object();
    json_object_set (dir, "fileval0000",
                     j_dirent_create ("FILEVAL", json_string ("0")));
    json_object_set (dir, "fileval0010",
                     j_dirent_create ("FILEVAL", json_string ("1")));
    json_object_set (dir, "fileval0200",
                     j_dirent_create ("FILEVAL", json_string ("2")));
    json_object_set (dir, "fileval3000",
                     j_dirent_create ("FILEVAL", json_string ("3")));
    json_object_set (dir, "fileval0004",
                     j_dirent_create ("FILEVAL", json_string ("4")));
    json_object_set (dir, "fileval0050",
                     j_dirent_create ("FILEVAL", json_string ("5")));
    json_object_set (dir, "fileval0600",
                     j_dirent_create ("FILEVAL", json_string ("6")));
    json_object_set (dir, "fileval7000",
                     j_dirent_create ("FILEVAL", json_string ("7")));
    json_object_set (dir, "fileval0008",
                     j_dirent_create ("FILEVAL", json_string ("8")));
    json_object_set (dir, "fileval0090",
                     j_dirent_create ("FILEVAL", json_string ("9")));
    json_object_set (dir, "fileval0a00",
                     j_dirent_create ("FILEVAL", json_string ("A")));
    json_object_set (dir, "filevalB000",
                     j_dirent_create ("FILEVAL", json_string ("b")));
    json_object_set (dir, "fileval000c",
                     j_dirent_create ("FILEVAL", json_string ("C")));
    json_object_set (dir, "fileval00D0",
                     j_dirent_create ("FILEVAL", json_string ("d")));
    json_object_set (dir, "fileval0e00",
                     j_dirent_create ("FILEVAL", json_string ("E")));
    json_object_set (dir, "filevalF000",
                     j_dirent_create ("FILEVAL", json_string ("f")));

    ok (kvs_util_json_hash ("sha1", dir, dir_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = json_object ();
    json_object_set (root, "dir", j_dirent_create ("DIRREF", dir_ref));

    ok (kvs_util_json_hash ("sha1", root, root_ref) == 0,
        "kvs_util_json_hash worked");

    cache_insert (cache, root_ref, cache_entry_create (root));

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* make three ready commits */
    create_ready_commit (cm, "fence1", "dir.fileval0200", "foo", 0);
    create_ready_commit (cm, "fence2", "dir.fileval0090", "bar", 0);
    /* NULL value --> delete */
    create_ready_commit (cm, "fence3", "dir.fileval00D0", NULL, 0);

    /* merge these three commits */
    commit_mgr_merge_ready_commits (cm);

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_DIRTY_CACHE_ENTRIES,
        "commit_process returns COMMIT_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (commit_iter_dirty_cache_entries (c, cache_noop_cb, NULL) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (commit_process (c, 1, root_ref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.fileval0200", "foo");
    verify_value (cache, newroot, "dir.fileval0090", "bar");
    verify_value (cache, newroot, "dir.fileval00D0", NULL);

    commit_mgr_remove_commit (cm, c);

    ok ((c = commit_mgr_get_ready_commit (cm)) == NULL,
        "commit_mgr_get_ready_commit returns NULL, no more commits");

    commit_mgr_destroy (cm);
    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    commit_mgr_basic_tests ();
    commit_mgr_merge_tests ();
    commit_basic_tests ();
    commit_basic_commit_process_test ();
    commit_process_root_missing ();
    commit_process_missing_ref ();
    commit_basic_commit_process_test_multiple_fences ();
    commit_basic_commit_process_test_multiple_fences_merge ();
    /* no need for dirty_cache_entries() test, as it is the most
     * "normal" situation and is tested throughout
     */
    commit_process_error_callbacks ();
    commit_process_invalid_operation ();
    commit_process_invalid_hash ();
    commit_process_follow_link ();
    commit_process_dirval_test ();
    commit_process_delete_test ();
    commit_process_delete_nosubdir_test ();
    commit_process_delete_filevalinpath_test ();
    commit_process_big_fileval ();
    commit_process_giant_dir ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
