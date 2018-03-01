#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libkvs/kvs.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_txn_private.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/kvstxn.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/treq.h"
#include "src/modules/kvs/kvs_util.h"

static int test_global = 5;

static int treeobj_hash (const char *hash_name, json_t *obj, blobref_t blobref)
{
    char *tmp = NULL;
    int rc = -1;

    if (!hash_name || !obj || !blobref) {
        errno = EINVAL;
        goto error;
    }

    if (treeobj_validate (obj) < 0)
        goto error;

    if (!(tmp = treeobj_encode (obj)))
        goto error;

    if (blobref_hash (hash_name, (uint8_t *)tmp, strlen (tmp), blobref) < 0)
        goto error;
    rc = 0;
 error:
    free (tmp);
    return rc;
}

static int cache_entry_set_treeobj (struct cache_entry *entry, const json_t *o)
{
    char *s = NULL;
    int saved_errno;
    int rc = -1;

    if (!entry || !o || treeobj_validate (o) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!(s = treeobj_encode (o)))
        goto done;
    if (cache_entry_set_raw (entry, s, strlen (s)) < 0)
        goto done;
    rc = 0;
 done:
    saved_errno = errno;
    free (s);
    errno = saved_errno;
    return rc;
}

/* convenience function */
static struct cache_entry *create_cache_entry_raw (void *data, int len)
{
    struct cache_entry *entry;
    int ret;

    assert (data);
    assert (len);

    entry = cache_entry_create ();
    assert (entry);
    ret = cache_entry_set_raw (entry, data, len);
    assert (ret == 0);
    return entry;
}

/* convenience function */
static struct cache_entry *create_cache_entry_treeobj (json_t *o)
{
    struct cache_entry *entry;
    int ret;

    assert (o);

    entry = cache_entry_create ();
    assert (entry);
    ret = cache_entry_set_treeobj (entry, o);
    assert (ret == 0);
    return entry;
}

/* Append a treeobj object containing
 *     { "key" : key, flags : <num>, "dirent" : <treeobj> } }
 * or
 *     { "key" : key, flags : <num>, "dirent" : null }
 * to a json array.
 */
void ops_append (json_t *array, const char *key, const char *value, int flags)
{
    json_t *op;
    json_t *dirent;

    if (value)
        dirent = treeobj_create_val ((void *)value, strlen (value));
    else
        dirent = json_null ();

    txn_encode_op (key, flags, dirent, &op);
    json_decref (dirent);
    json_array_append_new (array, op);
}

struct cache *create_cache_with_empty_rootdir (blobref_t ref)
{
    struct cache *cache;
    struct cache_entry *entry;
    json_t *rootdir;

    rootdir = treeobj_create_dir ();

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (treeobj_hash ("sha1", rootdir, ref) == 0,
        "treeobj_hash worked");
    ok ((entry = create_cache_entry_treeobj (rootdir)) != NULL,
        "create_cache_entry_treeobj works");
    cache_insert (cache, ref, entry);
    return cache;
}

void kvstxn_mgr_basic_tests (void)
{
    struct cache *cache;
    json_t *ops = NULL;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    treq_t *tr;
    blobref_t rootref;

    ok (kvstxn_mgr_create (NULL, NULL, NULL, NULL, NULL) == NULL
        && errno == EINVAL,
        "kvstxn_mgr_create fails with EINVAL on bad input");

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    ok (kvstxn_mgr_get_noop_stores (ktm) == 0,
        "kvstxn_mgr_get_noop_stores works");

    kvstxn_mgr_clear_noop_stores (ktm);

    ok ((tr = treq_create ("transaction1", 1, 0)) != NULL,
        "treq_create works");

    ok (kvstxn_mgr_process_transaction_request (ktm, tr) == 0,
        "kvstxn_mgr_process_transaction_request works");

    ok (kvstxn_mgr_ready_transaction_count (ktm) == 0,
        "kvstxn_mgr_ready_transaction_count is 0");

    ok (kvstxn_mgr_transaction_ready (ktm) == false,
        "kvstxn_mgr_transaction_ready says no transactions are ready");

    ok (kvstxn_mgr_get_ready_transaction (ktm) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL for no ready kvstxns");

    ops = json_array ();
    ops_append (ops, "key1", "1", 0);

    ok (treq_add_request_ops (tr, ops) == 0,
        "treq_add_request_ops add works");

    json_decref (ops);

    ok (kvstxn_mgr_process_transaction_request (ktm, tr) == 0,
        "kvstxn_mgr_process_transaction_request works");

    ok (kvstxn_mgr_ready_transaction_count (ktm) == 1,
        "kvstxn_mgr_ready_transaction_count is 1");

    ok (kvstxn_mgr_process_transaction_request (ktm, tr) == 0,
        "kvstxn_mgr_process_transaction_request works again");

    ok (kvstxn_mgr_ready_transaction_count (ktm) == 1,
        "kvstxn_mgr_ready_transaction_count is still 1, didn't double add");

    ok (kvstxn_mgr_transaction_ready (ktm) == true,
        "kvstxn_mgr_transaction_ready says a transaction is ready");

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns != NULL for ready kvstxns");

    kvstxn_mgr_remove_transaction (ktm, kt);

    ok (kvstxn_mgr_transaction_ready (ktm) == false,
        "kvstxn_mgr_transaction_ready says no transactions are ready");

    ok (kvstxn_mgr_get_ready_transaction (ktm) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL no ready kvstxns");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void create_ready_kvstxn (kvstxn_mgr_t *ktm,
                          const char *name,
                          const char *key,
                          const char *val,
                          int op_flags,
                          int transaction_flags)
{
    treq_t *tr;
    json_t *ops = NULL;

    ok ((tr = treq_create (name, 1, transaction_flags)) != NULL,
        "treq_create works");

    ops = json_array ();
    ops_append (ops, key, val, op_flags);

    ok (treq_add_request_ops (tr, ops) == 0,
        "treq_add_request_ops add works");

    json_decref (ops);

    ok (kvstxn_mgr_process_transaction_request (ktm, tr) == 0,
        "kvstxn_mgr_process_transaction_request works");

    ok (kvstxn_mgr_transaction_ready (ktm) == true,
        "kvstxn_mgr_transaction_ready says a kvstxn is ready");
}

void verify_ready_kvstxn (kvstxn_mgr_t *ktm,
                          json_t *names,
                          json_t *ops,
                          int flags,
                          const char *extramsg)
{
    json_t *o;
    kvstxn_t *kt;

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok ((o = kvstxn_get_names (kt)) != NULL,
        "kvstxn_get_names works");

    ok (json_equal (names, o) == true,
        "names match %s", extramsg);

    ok ((o = kvstxn_get_ops (kt)) != NULL,
        "kvstxn_get_ops works");

    ok (json_equal (ops, o) == true,
        "ops match %s", extramsg);

    ok (kvstxn_get_flags (kt) == flags,
        "flags do not match");
}

void clear_ready_kvstxns (kvstxn_mgr_t *ktm)
{
    kvstxn_t *kt;

    while ((kt = kvstxn_mgr_get_ready_transaction (ktm)))
        kvstxn_mgr_remove_transaction (ktm, kt);
}

void kvstxn_mgr_merge_tests (void)
{
    struct cache *cache;
    json_t *names, *ops = NULL;
    kvstxn_mgr_t *ktm;
    blobref_t rootref;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* test successful merge */

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "key2", "2", 0, 0);

    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    names = json_array ();
    json_array_append (names, json_string ("transaction1"));
    json_array_append (names, json_string ("transaction2"));

    ops = json_array ();
    ops_append (ops, "key1", "1", 0);
    ops_append (ops, "key2", "2", 0);

    verify_ready_kvstxn (ktm, names, ops, 0, "merged transaction");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_kvstxns (ktm);

    /* test unsuccessful merge */

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, FLUX_KVS_NO_MERGE);
    create_ready_kvstxn (ktm, "transaction2", "key2", "2", 0, 0);

    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    names = json_array ();
    json_array_append (names, json_string ("transaction1"));

    ops = json_array ();
    ops_append (ops, "key1", "1", 0);

    verify_ready_kvstxn (ktm, names, ops, FLUX_KVS_NO_MERGE, "unmerged transaction");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_kvstxns (ktm);

    /* test unsuccessful merge */

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "key2", "2", 0, FLUX_KVS_NO_MERGE);

    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    names = json_array ();
    json_array_append (names, json_string ("transaction1"));

    ops = json_array ();
    ops_append (ops, "key1", "1", 0);

    verify_ready_kvstxn (ktm, names, ops, 0, "unmerged transaction");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    clear_ready_kvstxns (ktm);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

int ref_noop_cb (kvstxn_t *kt, const char *ref, void *data)
{
    return 0;
}

int cache_noop_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    return 0;
}

void kvstxn_basic_tests (void)
{
    struct cache *cache;
    json_t *names, *ops = NULL;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t rootref;
    const char *namespace;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0x44);

    names = json_array ();
    json_array_append (names, json_string ("transaction1"));

    ops = json_array ();
    ops_append (ops, "key1", "1", 0);

    verify_ready_kvstxn (ktm, names, ops, 0x44, "basic test");

    json_decref (names);
    json_decref (ops);
    ops = NULL;

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_get_errnum (kt) == 0,
        "kvstxn_get_errnum returns no error");

    ok (kvstxn_get_aux_errnum (kt) == 0,
        "kvstxn_get_aux_errnum returns no error");

    ok (kvstxn_set_aux_errnum (kt, EINVAL) == EINVAL,
        "kvstxn_set_aux_errnum works");

    ok (kvstxn_get_aux_errnum (kt) == EINVAL,
        "kvstxn_get_aux_errnum gets EINVAL");

    ok (kvstxn_get_errnum (kt) == 0,
        "kvstxn_get_errnum still works");

    ok ((namespace = kvstxn_get_namespace (kt)) != NULL,
        "kvstxn_get_namespace returns non-NULL");

    ok (!strcmp (namespace, KVS_PRIMARY_NAMESPACE),
        "kvstxn_get_namespace returns correct string");

    ok (kvstxn_get_aux (kt) == &test_global,
        "kvstxn_get_aux returns correct pointer");

    ok (kvstxn_get_newroot_ref (kt) == NULL,
        "kvstxn_get_newroot_ref returns NULL when processing not complete");

    ok (kvstxn_iter_missing_refs (kt, ref_noop_cb, NULL) < 0,
        "kvstxn_iter_missing_refs returns < 0 for call on invalid state");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) < 0,
        "kvstxn_iter_dirty_cache_entries returns < 0 for call on invalid state");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

int cache_count_dirty_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    int *count = data;
    if (cache_entry_get_dirty (entry)) {
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
                             KVS_PRIMARY_NAMESPACE,
                             root_ref,
                             key,
                             NULL,
                             0)) != NULL,
        "lookup_create key %s", key);

    ok (lookup (lh) == true,
        "lookup found result");

    if (val) {
        test = treeobj_create_val (val, strlen (val));
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

void kvstxn_basic_kvstxn_process_test (void)
{
    struct cache *cache;
    int count = 0;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");

    kvstxn_mgr_remove_transaction (ktm, kt);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL, no more kvstxns");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_basic_kvstxn_process_test_multiple_transactions (void)
{
    struct cache *cache;
    int count = 0;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "dir.key2", "2", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    strcpy (rootref, newroot);

    /* get rid of the this kvstxn, we're done */
    kvstxn_mgr_remove_transaction (ktm, kt);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    count = 0;

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* why two? 1 for root (new dir added), 1 for dir.key2 (a new dir) */
    ok (count == 2,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");
    verify_value (cache, newroot, "dir.key2", "2");

    kvstxn_mgr_remove_transaction (ktm, kt);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL, no more kvstxns");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_basic_kvstxn_process_test_multiple_transactions_merge (void)
{
    struct cache *cache;
    int count = 0;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t rootref;
    const char *newroot;

    cache = create_cache_with_empty_rootdir (rootref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "foo.key1", "1", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "bar.key2", "2", 0, 0);

    /* merge ready kvstxns */
    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* why three? 1 for root, 1 for foo.key1 (a new dir), and 1 for
     * bar.key2 (a new dir)
     */

    ok (count == 3,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "foo.key1", "1");
    verify_value (cache, newroot, "bar.key2", "2");

    kvstxn_mgr_remove_transaction (ktm, kt);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL, no more kvstxns");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_basic_root_not_dir (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* make a non-dir root */
    root = treeobj_create_val ("abcd", 4);

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "val", "42", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    /* error is caught continuously */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR again");

    ok (kvstxn_get_errnum (kt) == EINVAL,
        "kvstxn_get_errnum return EINVAL");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

struct rootref_data {
    struct cache *cache;
    const char *rootref;
};

int rootref_cb (kvstxn_t *kt, const char *ref, void *data)
{
    struct rootref_data *rd = data;
    json_t *rootdir;
    struct cache_entry *entry;

    ok (strcmp (ref, rd->rootref) == 0,
        "missing root reference is what we expect it to be");

    ok ((rootdir = treeobj_create_dir ()) != NULL,
        "treeobj_create_dir works");

    ok ((entry = create_cache_entry_treeobj (rootdir)) != NULL,
        "create_cache_entry_treeobj works");

    cache_insert (rd->cache, ref, entry);

    return 0;
}

void kvstxn_process_root_missing (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t rootref;
    struct rootref_data rd;
    json_t *rootdir;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok ((rootdir = treeobj_create_dir ()) != NULL,
        "treeobj_create_dir works");

    ok (treeobj_hash ("sha1", rootdir, rootref) == 0,
        "treeobj_hash worked");

    json_decref (rootdir);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "key1", "1", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_LOAD_MISSING_REFS,
        "kvstxn_process returns KVSTXN_PROCESS_LOAD_MISSING_REFS");

    /* user forgot to call kvstxn_iter_missing_refs() test */
    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_LOAD_MISSING_REFS,
        "kvstxn_process returns KVSTXN_PROCESS_LOAD_MISSING_REFS again");

    rd.cache = cache;
    rd.rootref = rootref;

    ok (kvstxn_iter_missing_refs (kt, rootref_cb, &rd) == 0,
        "kvstxn_iter_missing_refs works for dirty cache entries");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    /* user forgot to call kvstxn_iter_dirty_cache_entries() test */
    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES again");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, rootref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "key1", "1");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

struct missingref_data {
    struct cache *cache;
    const char *dir_ref;
    json_t *dir;
};

int missingref_cb (kvstxn_t *kt, const char *ref, void *data)
{
    struct missingref_data *md = data;
    struct cache_entry *entry;

    ok (strcmp (ref, md->dir_ref) == 0,
        "missing reference is what we expect it to be");

    ok ((entry = create_cache_entry_treeobj (md->dir)) != NULL,
        "create_cache_entry_treeobj works");

    cache_insert (md->cache, ref, entry);

    return 0;
}

void kvstxn_process_missing_ref (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;
    struct missingref_data md;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "commit_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.val", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_LOAD_MISSING_REFS,
        "kvstxn_process returns KVSTXN_PROCESS_LOAD_MISSING_REFS");

    /* user forgot to call kvstxn_iter_missing_refs() test */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_LOAD_MISSING_REFS,
        "kvstxn_process returns KVSTXN_PROCESS_LOAD_MISSING_REFS again");

    md.cache = cache;
    md.dir_ref = dir_ref;
    md.dir = dir;

    ok (kvstxn_iter_missing_refs (kt, missingref_cb, &md) == 0,
        "kvstxn_iter_missing_refs works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    /* user forgot to call kvstxn_iter_dirty_cache_entries() test */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES again");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.val", "52");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

int ref_error_cb (kvstxn_t *kt, const char *ref, void *data)
{
    /* pick a weird errno */
    errno = ENOTTY;
    return -1;
}

int cache_error_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    kvstxn_cleanup_dirty_cache_entry (kt, entry);

    /* pick a weird errno */
    errno = EXDEV;
    return -1;
}

void kvstxn_process_error_callbacks (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    /* don't add dir entry, we want it to miss  */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.val", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_LOAD_MISSING_REFS,
        "kvstxn_process returns KVSTXN_PROCESS_LOAD_MISSING_REFS");

    errno = 0;
    ok (kvstxn_iter_missing_refs (kt, ref_error_cb, NULL) < 0
        && errno == ENOTTY,
        "kvstxn_iter_missing_refs errors on callback error & returns correct errno");

    /* insert cache entry now, want don't want missing refs on next
     * kvstxn_process call */
    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    errno = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_error_cb, NULL) < 0
        && errno == EXDEV,
        "kvstxn_iter_dirty_cache_entries errors on callback error & returns correct errno");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

struct error_partway_data {
    int total_calls;
    int success_returns;
};

int cache_error_partway_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    struct error_partway_data *epd = data;
    epd->total_calls++;
    if (epd->total_calls > 1)
        return -1;
    epd->success_returns++;
    /* pick a weird errno */
    errno = EDOM;
    return 0;
}

void kvstxn_process_error_callbacks_partway (void)
{
    struct cache *cache;
    struct error_partway_data epd = { .total_calls = 0, .success_returns = 0};
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.fileA", "52", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "dir.fileB", "53", 0, 0);

    /* merge these kvstxns */
    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    errno = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_error_partway_cb, &epd) < 0
        && errno == EDOM,
        "kvstxn_iter_dirty_cache_entries errors on callback error & returns correct errno");

    ok (epd.total_calls == 2,
        "correct number of total calls to dirty cache callback");
    ok (epd.success_returns == 1,
        "correct number of successful returns from dirty cache callback");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_invalid_operation (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is an empty root */
    root = treeobj_create_dir ();

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", ".", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    /* error is caught continuously */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR again");

    ok (kvstxn_get_errnum (kt) == EINVAL,
        "kvstxn_get_errnum return EINVAL");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_malformed_operation (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    blobref_t root_ref;
    treq_t *tr;
    json_t *ops, *badop;

    cache = create_cache_with_empty_rootdir (root_ref);

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* Create ops array containing one bad op.
     */
    ops = json_array ();
    badop = json_pack ("{s:s s:i s:n}",
                       "key", "mykey",
                       "flags", 0,
                       "donuts"); // EPROTO: should be "dirent"
    ok (ops != NULL && badop != NULL
        && json_array_append_new (ops, badop) == 0,
        "created ops array with one malformed unlink op");

    /* Create treq_t and add ops array to it.
     */
    ok ((tr = treq_create ("malformed", 1, 0)) != NULL,
        "treq_create works");

    ok (treq_add_request_ops (tr, ops) == 0,
        "treq_add_request_ops add works");

    /* Submit treq_t to kvstxn_mgr
     */
    ok (kvstxn_mgr_process_transaction_request (ktm, tr) == 0,
        "kvstxn_mgr_process_transaction_request works");

    /* Process ready kvstxn and verify EPROTO error
     */
    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR
        && kvstxn_get_errnum (kt) == EPROTO,
        "kvstxn_process encountered EPROTO error");

    json_decref (ops);
    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}


void kvstxn_process_invalid_hash (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is an empty root */
    root = treeobj_create_dir ();

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "foobar",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.fileval", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    /* verify kvstxn_process() does not continue processing */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR on second call");

    ok (kvstxn_get_errnum (kt) == EINVAL,
        "kvstxn_get_errnum return EINVAL %d", kvstxn_get_errnum (kt));

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_follow_link (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     * "symlink" : symlink to "dir"
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));
    treeobj_insert_entry (root, "symlink", treeobj_create_symlink ("dir"));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));


    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "symlink.val", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "symlink.val", "52");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_dirval_test (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dir with { "val" : val to 42 }
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", dir);

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.val", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.val", "52");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_delete_test (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* NULL value --> delete */
    create_ready_kvstxn (ktm, "transaction1", "dir.val", NULL, 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.val", NULL);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_delete_nosubdir_test (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is an empty root */
    root = treeobj_create_dir ();

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* subdir doesn't exist for this key */
    /* NULL value --> delete */
    create_ready_kvstxn (ktm, "transaction1", "noexistdir.val", NULL, 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "noexistdir.val", NULL);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_delete_filevalinpath_test (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to dir_ref
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* val is in path */
    /* NULL value --> delete */
    create_ready_kvstxn (ktm, "transaction1", "dir.val.valbaz", NULL, 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.val.valbaz", NULL);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_bad_dirrefs (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dirref;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : dirref to [ dir_ref, dir_ref ]
     *
     * dir_ref
     * "val" : val w/ "42"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    dirref = treeobj_create_dirref (dir_ref);
    treeobj_append_blobref (dirref, dir_ref);

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", dirref);

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    create_ready_kvstxn (ktm, "transaction1", "dir.val", "52", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    /* error is caught continuously */
    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR again");

    ok (kvstxn_get_errnum (kt) == ENOTRECOVERABLE,
        "kvstxn_get_errnum return ENOTRECOVERABLE");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

struct cache_count {
    int treeobj_count;
    int total_count;
};

int cache_count_treeobj_cb (kvstxn_t *kt, struct cache_entry *entry, void *data)
{
    struct cache_count *cache_count = data;

    /* we count "raw-ness" of a cache entry by determining if the
     * cache entry holds a valid treeobj object.
     */
    if (cache_entry_get_treeobj (entry) != NULL)
        cache_count->treeobj_count++;
    cache_count->total_count++;

    return 0;
}

void kvstxn_process_big_fileval (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;
    const char *newroot;
    int bigstrsize = BLOBREF_MAX_STRING_SIZE * 2;
    char bigstr[bigstrsize];
    struct cache_count cache_count;
    int i;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "val" : val w/ "42"
     */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "val", treeobj_create_val ("42", 2));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* first kvstxn a small value, to make sure it ends up as json in
     * the cache */

    create_ready_kvstxn (ktm, "transaction1", "val", "smallstr", 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    cache_count.treeobj_count = 0;
    cache_count.total_count = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_treeobj_cb,
                                         &cache_count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (cache_count.treeobj_count == 1,
        "correct number of cache entries were treeobj");

    ok (cache_count.total_count == 1,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "val", "smallstr");

    kvstxn_mgr_remove_transaction (ktm, kt);

    /* next kvstxn a big value, to make sure it is not json in the
     * cache */

    memset (bigstr, '\0', bigstrsize);
    for (i = 0; i < bigstrsize - 1; i++)
        bigstr[i] = 'a';

    create_ready_kvstxn (ktm, "transaction2", "val", bigstr, 0, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    cache_count.treeobj_count = 0;
    cache_count.total_count = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_treeobj_cb,
                                         &cache_count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* this entry should be not be json, it's raw b/c large val
     * converted into valref, but with change there are now two dirty entries */

    ok (cache_count.treeobj_count == 1,
        "correct number of cache entries were treeobj");

    ok (cache_count.total_count == 2,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "val", bigstr);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

/* Test giant directory entry, as large json objects will iterate through
 * their entries randomly based on the internal hash data structure.
 */
void kvstxn_process_giant_dir (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    json_t *dir;
    blobref_t root_ref;
    blobref_t dir_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is.
     *
     * root
     * "dir" : dirref to dir_ref
     *
     * Mix up keys and upper/lower case to get different hash ordering
     * other than the "obvious" one.
     *
     * dir_ref
     * "val0000" : val to "0"
     * "val0010" : val to "1"
     * "val0200" : val to "2"
     * "val3000" : val to "3"
     * "val0004" : val to "4"
     * "val0050" : val to "5"
     * "val0600" : val to "6"
     * "val7000" : val to "7"
     * "val0008" : val to "8"
     * "val0090" : val to "9"
     * "val0a00" : val to "A"
     * "valB000" : val to "b"
     * "val000c" : val to "C"
     * "val00D0" : val to "d"
     * "val0e00" : val to "E"
     * "valF000" : val to "f"
     *
     */

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val0000", treeobj_create_val ("0", 1));
    treeobj_insert_entry (dir, "val0010", treeobj_create_val ("1", 1));
    treeobj_insert_entry (dir, "val0200", treeobj_create_val ("2", 1));
    treeobj_insert_entry (dir, "val3000", treeobj_create_val ("3", 1));
    treeobj_insert_entry (dir, "val0004", treeobj_create_val ("4", 1));
    treeobj_insert_entry (dir, "val0050", treeobj_create_val ("5", 1));
    treeobj_insert_entry (dir, "val0600", treeobj_create_val ("6", 1));
    treeobj_insert_entry (dir, "val7000", treeobj_create_val ("7", 1));
    treeobj_insert_entry (dir, "val0008", treeobj_create_val ("8", 1));
    treeobj_insert_entry (dir, "val0090", treeobj_create_val ("9", 1));
    treeobj_insert_entry (dir, "val0a00", treeobj_create_val ("A", 1));
    treeobj_insert_entry (dir, "valB000", treeobj_create_val ("b", 1));
    treeobj_insert_entry (dir, "val000c", treeobj_create_val ("C", 1));
    treeobj_insert_entry (dir, "val00D0", treeobj_create_val ("d", 1));
    treeobj_insert_entry (dir, "val0e00", treeobj_create_val ("E", 1));
    treeobj_insert_entry (dir, "valF000", treeobj_create_val ("f", 1));

    ok (treeobj_hash ("sha1", dir, dir_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, dir_ref, create_cache_entry_treeobj (dir));

    root = treeobj_create_dir ();
    treeobj_insert_entry (dir, "dir", treeobj_create_dirref (dir_ref));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /* make three ready kvstxns */
    create_ready_kvstxn (ktm, "transaction1", "dir.val0200", "foo", 0, 0);
    create_ready_kvstxn (ktm, "transaction2", "dir.val0090", "bar", 0, 0);
    /* NULL value --> delete */
    create_ready_kvstxn (ktm, "transaction3", "dir.val00D0", NULL, 0, 0);

    /* merge these three kvstxns */
    ok (kvstxn_mgr_merge_ready_transactions (ktm) == 0,
        "kvstxn_mgr_merge_ready_transactions success");

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    ok (kvstxn_iter_dirty_cache_entries (kt, cache_noop_cb, NULL) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "dir.val0200", "foo");
    verify_value (cache, newroot, "dir.val0090", "bar");
    verify_value (cache, newroot, "dir.val00D0", NULL);

    kvstxn_mgr_remove_transaction (ktm, kt);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) == NULL,
        "kvstxn_mgr_get_ready_transaction returns NULL, no more kvstxns");

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_append (void)
{
    struct cache *cache;
    int count = 0;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t valref_ref;
    blobref_t root_ref;
    const char *newroot;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * valref_ref
     * "ABCD"
     *
     * root_ref
     * "val" : val to "abcd"
     * "valref" : valref to valref_ref
     */

    blobref_hash ("sha1", "ABCD", 4, valref_ref);
    cache_insert (cache, valref_ref, create_cache_entry_raw (strdup ("ABCD"), 4));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "val", treeobj_create_val ("abcd", 4));
    treeobj_insert_entry (root, "valref", treeobj_create_val ("ABCD", 4));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /*
     * first test, append to a treeobj val
     */

    create_ready_kvstxn (ktm, "transaction1", "val", "efgh", FLUX_KVS_APPEND, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    count = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* 3 dirty entries, raw "abcd", raw "efgh", and a new root b/c val
     * has been changed into a valref. */
    ok (count == 3,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "val", "abcdefgh");

    kvstxn_mgr_remove_transaction (ktm, kt);

    /*
     * second test, append to a treeobj valref
     */

    create_ready_kvstxn (ktm, "transaction2", "valref", "EFGH", FLUX_KVS_APPEND, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    count = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* 2 dirty entries, raw "EFGH", and a new root b/c valref has an
     * additional blobref */
    ok (count == 2,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "valref", "ABCDEFGH");

    kvstxn_mgr_remove_transaction (ktm, kt);

    /*
     * third test, append to a non-existent value, it's like an insert
     */

    create_ready_kvstxn (ktm, "transaction3", "newval", "foobar", FLUX_KVS_APPEND, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES,
        "kvstxn_process returns KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES");

    count = 0;
    ok (kvstxn_iter_dirty_cache_entries (kt, cache_count_dirty_cb, &count) == 0,
        "kvstxn_iter_dirty_cache_entries works for dirty cache entries");

    /* 1 dirty entries, root simply has a new val in it */
    ok (count == 1,
        "correct number of cache entries were dirty");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_FINISHED,
        "kvstxn_process returns KVSTXN_PROCESS_FINISHED");

    ok ((newroot = kvstxn_get_newroot_ref (kt)) != NULL,
        "kvstxn_get_newroot_ref returns != NULL when processing complete");

    verify_value (cache, newroot, "newval", "foobar");

    kvstxn_mgr_remove_transaction (ktm, kt);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

void kvstxn_process_append_errors (void)
{
    struct cache *cache;
    kvstxn_mgr_t *ktm;
    kvstxn_t *kt;
    json_t *root;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This root is
     *
     * root_ref
     * "dir" : empty directory
     * "symlink" : symlink to "dir"
     */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dir", treeobj_create_dir ());
    treeobj_insert_entry (root, "symlink", treeobj_create_symlink ("dir"));

    ok (treeobj_hash ("sha1", root, root_ref) == 0,
        "treeobj_hash worked");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    ok ((ktm = kvstxn_mgr_create (cache,
                                  KVS_PRIMARY_NAMESPACE,
                                  "sha1",
                                  NULL,
                                  &test_global)) != NULL,
        "kvstxn_mgr_create works");

    /*
     * append to a dir, should get EISDIR
     */

    create_ready_kvstxn (ktm, "transaction1", "dir", "1", FLUX_KVS_APPEND, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    ok (kvstxn_get_errnum (kt) == EISDIR,
        "kvstxn_get_errnum return EISDIR");

    kvstxn_mgr_remove_transaction (ktm, kt);

    /*
     * append to a symlink, should get EOPNOTSUPP
     */

    create_ready_kvstxn (ktm, "transaction2", "symlink", "2", FLUX_KVS_APPEND, 0);

    ok ((kt = kvstxn_mgr_get_ready_transaction (ktm)) != NULL,
        "kvstxn_mgr_get_ready_transaction returns ready kvstxn");

    ok (kvstxn_process (kt, 1, root_ref) == KVSTXN_PROCESS_ERROR,
        "kvstxn_process returns KVSTXN_PROCESS_ERROR");

    ok (kvstxn_get_errnum (kt) == EOPNOTSUPP,
        "kvstxn_get_errnum return EOPNOTSUPP");

    kvstxn_mgr_remove_transaction (ktm, kt);

    kvstxn_mgr_destroy (ktm);
    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    kvstxn_mgr_basic_tests ();
    kvstxn_mgr_merge_tests ();
    kvstxn_basic_tests ();
    kvstxn_basic_kvstxn_process_test ();
    kvstxn_basic_kvstxn_process_test_multiple_transactions ();
    kvstxn_basic_kvstxn_process_test_multiple_transactions_merge ();
    kvstxn_basic_root_not_dir ();
    kvstxn_process_root_missing ();
    kvstxn_process_missing_ref ();
    /* no need for dirty_cache_entries() test, as it is the most
     * "normal" situation and is tested throughout
     */
    kvstxn_process_error_callbacks ();
    kvstxn_process_error_callbacks_partway ();
    kvstxn_process_invalid_operation ();
    kvstxn_process_malformed_operation ();
    kvstxn_process_invalid_hash ();
    kvstxn_process_follow_link ();
    kvstxn_process_dirval_test ();
    kvstxn_process_delete_test ();
    kvstxn_process_delete_nosubdir_test ();
    kvstxn_process_delete_filevalinpath_test ();
    kvstxn_process_bad_dirrefs ();
    kvstxn_process_big_fileval ();
    kvstxn_process_giant_dir ();
    kvstxn_process_append ();
    kvstxn_process_append_errors ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
