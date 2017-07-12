#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/common/libkvs/json_dirent.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/commit.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/fence.h"
#include "src/modules/kvs/json_util.h"
#include "src/modules/kvs/types.h"

static int test_global = 5;

struct cache_count {
    int store_count;
    int dirty_count;
};

struct cache *create_cache_with_empty_rootdir (href_t ref)
{
    struct cache *cache;
    struct cache_entry *hp;
    json_object *rootdir = Jnew ();

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (json_hash ("sha1", rootdir, ref) == 0,
        "json_hash worked");
    ok ((hp = cache_entry_create (rootdir)) != NULL,
        "cache_entry_create works");
    cache_insert (cache, ref, hp);
    return cache;
}

void commit_mgr_basic_tests (void)
{
    struct cache *cache;
    json_object *ops = NULL;
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

    dirent_append (&ops,
                   "key1",
                   dirent_create ("FILEVAL",
                                  json_object_new_string ("1")));

    ok (fence_add_request_data (f, ops) == 0,
        "fence_add_request_data add works");

    Jput (ops);

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
    json_object *ops = NULL;

    ok ((f = fence_create (name, 1, flags)) != NULL,
        "fence_create works");

    if (val)
        dirent_append (&ops,
                       key,
                       dirent_create ("FILEVAL", json_object_new_string (val)));
    else
        dirent_append (&ops, key, NULL);

    ok (fence_add_request_data (f, ops) == 0,
        "fence_add_request_data add works");

    Jput (ops);

    ok (commit_mgr_add_fence (cm, f) == 0,
        "commit_mgr_add_fence works");

    ok (commit_mgr_process_fence_request (cm, f) == 0,
        "commit_mgr_process_fence_request works");

    ok (commit_mgr_commits_ready (cm) == true,
        "commit_mgr_commits_ready says a commit is ready");
}

void verify_ready_commit (commit_mgr_t *cm,
                          json_object *names,
                          json_object *ops,
                          const char *extramsg)
{
    json_object *o;
    commit_t *c;
    fence_t *f;

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok ((f = commit_get_fence (c)) != NULL,
        "commit_get_fence returns commit fence");

    ok ((o = fence_get_json_names (f)) != NULL,
        "fence_get_json_names works");

    ok (json_compare (names, o) == true,
        "names match %s", extramsg);

    ok ((o = fence_get_json_ops (f)) != NULL,
        "fence_get_json_ops works");

    ok (json_compare (ops, o) == true,
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
    json_object *names, *ops = NULL;
    commit_mgr_t *cm;
    href_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    /* test successful merge */

    create_ready_commit (cm, "fence1", "key1", "1", 0);
    create_ready_commit (cm, "fence2", "key2", "2", 0);

    commit_mgr_merge_ready_commits (cm);

    names = Jnew_ar ();
    Jadd_ar_str (names, "fence1");
    Jadd_ar_str (names, "fence2");

    dirent_append (&ops,
                   "key1",
                   dirent_create ("FILEVAL", json_object_new_string ("1")));
    dirent_append (&ops,
                   "key2",
                   dirent_create ("FILEVAL", json_object_new_string ("2")));

    verify_ready_commit (cm, names, ops, "merged fence");

    Jput (names);
    Jput (ops);
    ops = NULL;

    clear_ready_commits (cm);

    commit_mgr_remove_fence (cm, "fence1");
    commit_mgr_remove_fence (cm, "fence2");

    /* test unsuccessful merge */

    create_ready_commit (cm, "fence1", "key1", "1", KVS_NO_MERGE);
    create_ready_commit (cm, "fence2", "key2", "2", 0);

    commit_mgr_merge_ready_commits (cm);

    names = Jnew_ar ();
    Jadd_ar_str (names, "fence1");

    dirent_append (&ops,
                   "key1",
                   dirent_create ("FILEVAL", json_object_new_string ("1")));

    verify_ready_commit (cm, names, ops, "unmerged fence");

    Jput (names);
    Jput (ops);
    ops = NULL;

    clear_ready_commits (cm);

    commit_mgr_remove_fence (cm, "fence1");
    commit_mgr_remove_fence (cm, "fence2");

    /* test unsuccessful merge */

    create_ready_commit (cm, "fence1", "key1", "1", 0);
    create_ready_commit (cm, "fence2", "key2", "2", KVS_NO_MERGE);

    commit_mgr_merge_ready_commits (cm);

    names = Jnew_ar ();
    Jadd_ar_str (names, "fence1");

    dirent_append (&ops,
                   "key1",
                   dirent_create ("FILEVAL", json_object_new_string ("1")));

    verify_ready_commit (cm, names, ops, "unmerged fence");

    Jput (names);
    Jput (ops);
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
    json_object *names, *ops = NULL;
    commit_mgr_t *cm;
    commit_t *c;
    href_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((cm = commit_mgr_create (cache, "sha1", &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_commit (cm, "fence1", "key1", "1", 0);

    names = Jnew_ar ();
    Jadd_ar_str (names, "fence1");

    dirent_append (&ops,
                   "key1",
                   dirent_create ("FILEVAL", json_object_new_string ("1")));

    verify_ready_commit (cm, names, ops, "basic test");

    Jput (names);
    Jput (ops);
    ops = NULL;

    ok ((c = commit_mgr_get_ready_commit (cm)) != NULL,
        "commit_mgr_get_ready_commit returns ready commit");

    ok (commit_get_errnum (c) == 0,
        "commit_get_errnum returns no error");

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
    struct cache_count *cc = data;
    if (cache_entry_get_content_store_flag (hp)) {
        if (cc)
            cc->store_count++;
        cache_entry_set_content_store_flag (hp, false);
    }
    if (cache_entry_get_dirty (hp)) {
        if (cc)
            cc->dirty_count++;
    }
    return 0;
}

void verify_value (struct cache *cache,
                   const char *root_ref,
                   const char *key,
                   const char *val)
{
    lookup_t *lh;
    json_object *test, *o;

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
        test = json_object_new_string (val);
        ok ((o = lookup_get_value (lh)) != NULL,
            "lookup_get_value returns non-NULL as expected");
        ok (json_compare (test, o) == true,
            "lookup_get_value returned matching value");
        Jput (test);
    }
    else
        ok (lookup_get_value (lh) == NULL,
            "lookup_get_value returns NULL as expected");

    lookup_destroy (lh);
}

void commit_basic_commit_process_test (void)
{
    struct cache *cache;
    struct cache_count cc = { .store_count = 0, .dirty_count = 0 };
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

    ok (commit_iter_dirty_cache_entries (c, cache_count_cb, &cc) == 0,
        "commit_iter_dirty_cache_entries works for dirty cache entries");

    ok (cc.store_count == 1,
        "correct number of cache entries had to be stored");

    ok (cc.dirty_count == 1,
        "correct number of cache entries were dirty");

    ok (commit_process (c, 1, rootref) == COMMIT_PROCESS_FINISHED,
        "commit_process returns COMMIT_PROCESS_FINISHED");

    ok ((newroot = commit_get_newroot_ref (c)) != NULL,
        "commit_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");

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
    json_object *rootdir = Jnew ();
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
    json_object *rootdir = Jnew ();
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok (json_hash ("sha1", rootdir, rootref) == 0,
        "json_hash worked");

    Jput (rootdir);

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
    json_object *dir;
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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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

void commit_process_invalid_operation (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));
    json_object_object_add (root,
                            "linkval",
                            dirent_create ("LINKVAL",
                                           json_object_new_string ("dir")));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    root = Jnew ();
    json_object_object_add (root,
                            "dirval",
                            dirent_create ("DIRVAL", dir));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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

void commit_process_big_fileval (void) {
    struct cache *cache;
    commit_mgr_t *cm;
    commit_t *c;
    json_object *root;
    json_object *dir;
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

    dir = Jnew();
    json_object_object_add (dir,
                            "fileval",
                            dirent_create ("FILEVAL",
                                           json_object_new_string ("42")));

    ok (json_hash ("sha1", dir, dir_ref) == 0,
        "json_hash worked");

    cache_insert (cache, dir_ref, cache_entry_create (dir));

    root = Jnew ();
    json_object_object_add (root, "dir", dirent_create ("DIRREF", dir_ref));

    ok (json_hash ("sha1", root, root_ref) == 0,
        "json_hash worked");

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

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    commit_mgr_basic_tests ();
    commit_mgr_merge_tests ();
    commit_basic_tests ();
    commit_basic_commit_process_test ();
    commit_process_root_missing ();
    commit_process_missing_ref ();
    /* no need for dirty_cache_entries() test, as it is the most
     * "normal" situation and is tested throughout
     */
    commit_process_error_callbacks ();
    commit_process_invalid_operation ();
    commit_process_invalid_hash ();
    commit_process_follow_link ();
    commit_process_dirval_test ();
    commit_process_delete_test ();
    commit_process_big_fileval ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
