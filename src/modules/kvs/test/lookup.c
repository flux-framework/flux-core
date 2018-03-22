#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <limits.h>
#include <jansson.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/treeobj.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/kvs_util.h"
#include "src/common/libutil/blobref.h"

int aux_global;

struct lookup_ref_data
{
    const char *ref;
    int count;
};

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

int lookup_ref (lookup_t *c,
                const char *ref,
                void *data)
{
    struct lookup_ref_data *ld = data;
    if (ld) {
        ld->ref = ref;
        ld->count++;
    }
    return 0;
}

int lookup_ref_error (lookup_t *c,
                      const char *ref,
                      void *data)
{
    /* pick a weird errno */
    errno = EMLINK;
    return -1;
}

void setup_kvsroot (kvsroot_mgr_t *krm,
                    struct cache *cache,
                    const char *ref,
                    uint32_t owner)
{
    struct kvsroot *root;

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         KVS_PRIMARY_NAMESPACE,
                                         owner,
                                         0)) != NULL,
        "kvsroot_mgr_create_root works");

    kvsroot_setroot (krm, root, ref, 0);
}

void basic_api (void)
{
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    const char *tmp;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");
    setup_kvsroot (krm, cache, "root.ref.foo", 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             42,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "path.bar",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ,
                             NULL,
                             &aux_global)) != NULL,
        "lookup_create works");
    ok (lookup_validate (lh) == true,
        "lookup_validate works");
    ok (lookup_get_current_epoch (lh) == 42,
        "lookup_get_current_epoch works");
    ok ((tmp = lookup_get_namespace (lh)) != NULL,
        "lookup_get_namespace works");
    ok (!strcmp (tmp, KVS_PRIMARY_NAMESPACE),
        "lookup_get_namespace returns correct string");
    ok (lookup_set_current_epoch (lh, 43) == 0,
        "lookup_set_current_epoch works");
    ok (lookup_get_current_epoch (lh) == 43,
        "lookup_get_current_epoch works");
    ok (lookup_get_aux_data (lh) == &aux_global,
        "lookup_get_aux_data returns correct aux pointer");
    ok (lookup_get_aux_errnum (lh) == 0,
        "lookup_get_aux_errnum returns no error");
    ok (lookup_set_aux_errnum (lh, EINVAL) == EINVAL,
        "lookup_set_aux_errnum works");
    ok (lookup_get_aux_errnum (lh) == EINVAL,
        "lookup_get_aux_errnum gets EINVAL");

    lookup_destroy (lh);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

void basic_api_errors (void)
{
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;

    ok (lookup_create (NULL,
                       NULL,
                       0,
                       NULL,
                       NULL,
                       NULL,
                       FLUX_ROLE_OWNER,
                       0,
                       0,
                       NULL,
                       NULL) == NULL,
        "lookup_create fails on bad input");

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");
    setup_kvsroot (krm, cache, "root.ref.foo", 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             42,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "path.bar",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create works");

    ok (lookup_get_errnum (lh) == EINVAL,
        "lookup_get_errnum returns EINVAL b/c lookup not yet started");
    ok (lookup_get_value (lh) == NULL,
        "lookup_get_value fails b/c lookup not yet started");
    ok (lookup_iter_missing_refs (lh, lookup_ref, NULL) < 0,
        "lookup_iter_missing_refs fails b/c lookup not yet started");

    ok (lookup_validate (NULL) == false,
        "lookup_validate fails on NULL pointer");
    ok (lookup (NULL) == true,
        "lookup does not segfault on NULL pointer");
    ok (lookup_get_errnum (NULL) == EINVAL,
        "lookup_get_errnum returns EINVAL on NULL pointer");
    ok (lookup_get_value (NULL) == NULL,
        "lookup_get_value fails on NULL pointer");
    ok (lookup_iter_missing_refs (NULL, lookup_ref, NULL) < 0,
        "lookup_iter_missing_refs fails on NULL pointer");
    ok (lookup_get_current_epoch (NULL) < 0,
        "lookup_get_current_epoch fails on NULL pointer");
    ok (lookup_get_namespace (NULL) == NULL,
        "lookup_get_namespace fails on NULL pointer");
    ok (lookup_get_aux_data (NULL) == NULL,
        "lookup_get_aux_data fails on NULL pointer");
    ok (lookup_set_current_epoch (NULL, 42) < 0,
        "lookup_set_current_epoch fails on NULL pointer");
    /* lookup_destroy ok on NULL pointer */
    lookup_destroy (NULL);

    lookup_destroy (lh);

    /* Now lh destroyed */

    ok (lookup_validate (lh) == false,
        "lookup_validate fails on bad pointer");
    ok (lookup (lh) == true,
        "lookup does not segfault on bad pointer");
    ok (lookup_get_errnum (lh) == EINVAL,
        "lookup_get_errnum returns EINVAL on bad pointer");
    ok (lookup_get_value (lh) == NULL,
        "lookup_get_value fails on bad pointer");
    ok (lookup_iter_missing_refs (lh, lookup_ref, NULL) < 0,
        "lookup_iter_missing_refs fails on bad pointer");
    ok (lookup_get_current_epoch (lh) < 0,
        "lookup_get_current_epoch fails on bad pointer");
    ok (lookup_get_namespace (lh) == NULL,
        "lookup_get_namespace fails on bad pointer");
    ok (lookup_get_aux_data (lh) == NULL,
        "lookup_get_aux_data fails on bad pointer");
    ok (lookup_set_current_epoch (lh, 42) < 0,
        "lookup_set_current_epoch fails on bad pointer");
    /* lookup_destroy ok on bad pointer */
    lookup_destroy (lh);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

void check_common (lookup_t *lh,
                   bool lookup_result,
                   int get_errnum_result,
                   bool check_is_val_treeobj,
                   json_t *get_value_result,
                   int missing_ref_count,
                   const char *missing_ref_result,
                   const char *msg,
                   bool destroy_lookup)
{
    json_t *val;
    struct lookup_ref_data ld = { .ref = NULL, .count = 0 };

    ok (lookup (lh) == lookup_result,
        "%s: lookup matched result", msg);
    ok (lookup_get_errnum (lh) == get_errnum_result,
        "%s: lookup_get_errnum returns expected errnum %d", msg, lookup_get_errnum (lh));
    if (check_is_val_treeobj) {
        ok ((val = lookup_get_value (lh)) != NULL,
            "%s: lookup_get_value returns non-NULL as expected", msg);
        if (val) {
            ok (treeobj_is_val (val) == true,
                "%s: lookup_get_value returned treeobj val as expected", msg);
            json_decref (val);
        }
        else {
            ok (false, "%s: lookup_get_value returned treeobj val as expected", msg);
        }
    }
    else {
        if (get_value_result) {
            ok ((val = lookup_get_value (lh)) != NULL,
                "%s: lookup_get_value returns non-NULL as expected", msg);
            if (val) {
                ok (json_equal (get_value_result, val) == true,
                    "%s: lookup_get_value returned matching value", msg);
                json_decref (val);
            }
            else {
                ok (false, "%s: lookup_get_value returned matching value", msg);
            }
        }
        else {
            ok ((val = lookup_get_value (lh)) == NULL,
                "%s: lookup_get_value returns NULL as expected", msg);
            json_decref (val);             /* just in case error */
        }
    }
    if (missing_ref_count == 1) {
        if (missing_ref_result) {
            ok (lookup_iter_missing_refs (lh, lookup_ref, &ld) == 0,
            "%s: lookup_iter_missing_refs success", msg);

            ok (ld.count == missing_ref_count,
                "%s: missing ref returned one missing refs", msg);

            if (ld.ref) {
                ok (strcmp (ld.ref, missing_ref_result) == 0,
                    "%s: missing ref returned matched expectation", msg);
            }
            else {
                ok (false, "%s: missing ref returned matched expectation", msg);
            }
        }
        else {
            ok (lookup_iter_missing_refs (lh, lookup_ref, &ld) < 0,
                "%s: lookup_iter_missing_refs fails as expected", msg);
        }
    }
    else {
        /* if missing_ref is > 1, we only care about number of missing refs,
         * as order isn't important.
         */
        ok (lookup_iter_missing_refs (lh, lookup_ref, &ld) == 0,
            "%s: lookup_iter_missing_refs fails as expected", msg);

        ok (ld.count == missing_ref_count,
            "%s: missing ref returned number of expected missing refs", msg);
    }


    if (destroy_lookup)
        lookup_destroy (lh);
}

void check_value (lookup_t *lh,
                  json_t *get_value_result,
                  const char *msg)
{
    check_common (lh,
                  true,
                  0,
                  false,
                  get_value_result,
                  1,
                  NULL,
                  msg,
                  true);
}

void check_treeobj_val_result (lookup_t *lh,
                               const char *msg)
{
    check_common (lh,
                  true,
                  0,
                  true,
                  NULL,         /* doesn't matter */
                  1,
                  NULL,
                  msg,
                  true);
}

void check_stall (lookup_t *lh,
                  int get_errnum_result,
                  int missing_ref_count,
                  const char *missing_ref_result,
                  const char *msg)
{
    check_common (lh,
                  false,
                  get_errnum_result,
                  false,
                  NULL,
                  missing_ref_count,
                  missing_ref_result,
                  msg,
                  false);
}

void check_error (lookup_t *lh,
                  int get_errnum_result,
                  const char *msg)
{
    check_common (lh,
                  true,
                  get_errnum_result,
                  false,
                  NULL,
                  1,
                  NULL,
                  msg,
                  true);
}

/* lookup tests on root dir */
void lookup_root (void) {
    json_t *root;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t valref_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * root_ref
     * treeobj dir, no entries
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref);
    cache_insert (cache, valref_ref, create_cache_entry_raw (strdup ("abcd"), 4));

    root = treeobj_create_dir ();
    treeobj_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 0);

    /* flags = 0, should error EISDIR */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on root, no flags, works");
    check_error (lh, EISDIR, "root no flags");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, works");
    check_value (lh, root, "root w/ FLUX_KVS_READDIR");

    /* flags = FLUX_KVS_TREEOBJ, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_TREEOBJ, works");
    test = treeobj_create_dirref (root_ref);
    check_value (lh, test, "root w/ FLUX_KVS_TREEOBJ");
    json_decref (test);

    /* flags = FLUX_KVS_READDIR, bad root_ref, should error EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             valref_ref,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, bad root_ref, should EINVAL");
    check_error (lh, EINVAL, "root w/ FLUX_KVS_READDIR, bad root_ref, should EINVAL");

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup basic tests */
void lookup_basic (void) {
    json_t *root;
    json_t *dirref;
    json_t *dirref_test;
    json_t *dir;
    json_t *valref_multi;
    json_t *valref_multi_with_dirref;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t valref_ref;
    blobref_t valref2_ref;
    blobref_t dirref_ref;
    blobref_t dirref_test_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * valref2_ref
     * "efgh"
     *
     * dirref_test_ref
     * "dummy" : val to "dummy"
     *
     * dirref_ref
     * "valref" : valref to valref_ref
     * "valref_with_dirref" : valref to dirref_ref
     * "valref_multi" : valref to [ valref_ref, valref2_ref ]
     * "valref_multi_with_dirref" : valref to [ valref_ref, dirref_test_ref ]
     * "val" : val to "foo"
     * "dir" : dir w/ "val" : val to "bar"
     * "symlink" : symlink to "baz"
     *
     * root_ref
     * "dirref" : dirref to dirref_ref
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref);
    cache_insert (cache, valref_ref, create_cache_entry_raw (strdup ("abcd"), 4));

    blobref_hash ("sha1", "efgh", 4, valref2_ref);
    cache_insert (cache, valref2_ref, create_cache_entry_raw (strdup ("efgh"), 4));

    dirref_test = treeobj_create_dir ();
    treeobj_insert_entry (dirref_test, "dummy", treeobj_create_val ("dummy", 5));

    treeobj_hash ("sha1", dirref_test, dirref_test_ref);
    cache_insert (cache, dirref_test_ref, create_cache_entry_treeobj (dirref_test));

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("bar", 3));

    dirref = treeobj_create_dir ();
    treeobj_insert_entry (dirref, "valref", treeobj_create_valref (valref_ref));
    treeobj_insert_entry (dirref, "valref_with_dirref", treeobj_create_valref (dirref_test_ref));
    treeobj_insert_entry (dirref, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref, "dir", dir);
    treeobj_insert_entry (dirref, "symlink", treeobj_create_symlink ("baz"));

    valref_multi = treeobj_create_valref (valref_ref);
    treeobj_append_blobref (valref_multi, valref2_ref);

    treeobj_insert_entry (dirref, "valref_multi", valref_multi);

    valref_multi_with_dirref = treeobj_create_valref (valref_ref);
    treeobj_append_blobref (valref_multi_with_dirref, dirref_test_ref);

    treeobj_insert_entry (dirref, "valref_multi_with_dirref", valref_multi_with_dirref);

    treeobj_hash ("sha1", dirref, dirref_ref);
    cache_insert (cache, dirref_ref, create_cache_entry_treeobj (dirref));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref", treeobj_create_dirref (dirref_ref));

    treeobj_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 0);

    /* lookup dir via dirref */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref");
    check_value (lh, dirref, "lookup dirref");

    /* lookup value via valref */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.valref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.valref");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "lookup dirref.valref");
    json_decref (test);

    /* lookup value via valref_with_dirref
     * - in this case user accidentally put a dirref in a valref
     *    object.  It succeeds, but we get the junk raw data of the
     *    treeobj of whatever the dirref was pointing to.
     */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.valref_with_dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref.valref_with_dirref");
    check_treeobj_val_result (lh, "lookup dirref.valref_with_dirref");

    /* Lookup value via valref with multiple blobrefs */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.valref_multi",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on valref_multi");
    test = treeobj_create_val ("abcdefgh", 8);
    check_value (lh, test, "lookup valref_multi");
    json_decref (test);

    /* lookup value via valref_multi_with_dirref
     * - in this case user accidentally put a dirref in a valref
     *    object.  It succeeds, but we get the junk raw data of the
     *    treeobj of whatever the dirref was pointing to.
     */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.valref_multi_with_dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref.valref_multi_with_dirref");
    check_treeobj_val_result (lh, "lookup dirref.valref_multi_with_dirref");

    /* lookup value via val */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup dirref.val");
    json_decref (test);

    /* lookup dir via dir */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.dir",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.dir");
    check_value (lh, dir, "lookup dirref.dir");

    /* lookup symlink */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlink");
    test = treeobj_create_symlink ("baz");
    check_value (lh, test, "lookup dirref.symlink");
    json_decref (test);

    /* lookup dirref treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref (treeobj)");
    test = treeobj_create_dirref (dirref_ref);
    check_value (lh, test, "lookup dirref treeobj");
    json_decref (test);

    /* lookup valref treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.valref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.valref (treeobj)");
    test = treeobj_create_valref (valref_ref);
    check_value (lh, test, "lookup dirref.valref treeobj");
    json_decref (test);

    /* lookup val treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.val",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.val (treeobj)");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup dirref.val treeobj");
    json_decref (test);

    /* lookup dir treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.dir",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.dir (treeobj)");
    check_value (lh, dir, "lookup dirref.dir treeobj");

    /* lookup symlink treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref.symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_TREEOBJ,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlink (treeobj)");
    test = treeobj_create_symlink ("baz");
    check_value (lh, test, "lookup dirref.symlink treeobj");
    json_decref (test);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup tests reach an error or "non-good" result */
void lookup_errors (void) {
    json_t *root;
    json_t *dirref;
    json_t *dir;
    json_t *dirref_multi;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t dirref_ref;
    blobref_t valref_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * dirref_ref
     * "val" : val to "bar"
     *
     * root_ref
     * "symlink" : symlink to "symlinkstr"
     * "symlink1" : symlink to "symlink2"
     * "symlink2" : symlink to "symlink1"
     * "val" : val to "foo"
     * "valref" : valref to valref_ref
     * "dirref" : dirref to dirref_ref
     * "dir" : dir w/ "val" : val to "baz"
     * "dirref_bad" : dirref to valref_ref
     * "dirref_multi" : dirref to [ dirref_ref, dirref_ref ]
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref);
    cache_insert (cache, valref_ref, create_cache_entry_raw (strdup ("abcd"), 4));

    dirref = treeobj_create_dir ();
    treeobj_insert_entry (dirref, "val", treeobj_create_val ("bar", 3));
    treeobj_hash ("sha1", dirref, dirref_ref);
    cache_insert (cache, dirref_ref, create_cache_entry_treeobj (dirref));

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("baz", 3));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "symlink", treeobj_create_symlink ("symlinkstr"));
    treeobj_insert_entry (root, "symlink1", treeobj_create_symlink ("symlink2"));
    treeobj_insert_entry (root, "symlink2", treeobj_create_symlink ("symlink1"));
    treeobj_insert_entry (root, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (root, "valref", treeobj_create_valref (valref_ref));
    treeobj_insert_entry (root, "dirref", treeobj_create_dirref (dirref_ref));
    treeobj_insert_entry (root, "dir", dir);
    treeobj_insert_entry (root, "dirref_bad", treeobj_create_dirref (valref_ref));

    dirref_multi = treeobj_create_dirref (dirref_ref);
    treeobj_append_blobref (dirref_multi, dirref_ref);

    treeobj_insert_entry (root, "dirref_multi", dirref_multi);

    treeobj_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 0);

    /* Lookup non-existent field.  Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "foo",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on bad path in path");
    check_value (lh, NULL, "lookup bad path");

    /* Lookup path w/ val in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "val.foo",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on val in path");
    check_value (lh, NULL, "lookup val in path");

    /* Lookup path w/ valref in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "valref.foo",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on valref in path");
    check_value (lh, NULL, "lookup valref in path");

    /* Lookup path w/ dir in middle, should get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dir.foo",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dir in path");
    check_error (lh, ENOTRECOVERABLE, "lookup dir in path");

    /* Lookup path w/ infinite link loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "symlink1",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on link loop");
    check_error (lh, ELOOP, "lookup infinite links");

    /* Lookup a dirref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref");
    check_error (lh, EINVAL, "lookup dirref, expecting link");

    /* Lookup a dir, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dir",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dir");
    check_error (lh, EINVAL, "lookup dir, expecting link");

    /* Lookup a valref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "valref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on valref");
    check_error (lh, EINVAL, "lookup valref, expecting link");

    /* Lookup a val, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "val",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on val");
    check_error (lh, EINVAL, "lookup val, expecting link");

    /* Lookup a dirref, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref");
    check_error (lh, EISDIR, "lookup dirref, not expecting dirref");

    /* Lookup a dir, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dir",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dir");
    check_error (lh, EISDIR, "lookup dir, not expecting dir");

    /* Lookup a valref, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "valref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on valref");
    check_error (lh, ENOTDIR, "lookup valref, expecting dir");

    /* Lookup a val, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "val",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on val");
    check_error (lh, ENOTDIR, "lookup val, expecting dir");

    /* Lookup a symlink, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK | FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on symlink");
    check_error (lh, ENOTDIR, "lookup symlink, expecting dir");

    /* Lookup a dirref that doesn't point to a dir, should get ENOTRECOVERABLE. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref_bad",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref_bad");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_bad");

    /* Lookup a dirref that doesn't point to a dir, in middle of path,
     * should get ENOTRECOVERABLE. */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref_bad.val",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref_bad, in middle of path");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_bad, in middle of path");

    /* Lookup with an invalid root_ref, should get EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             valref_ref,
                             "val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on bad root_ref");
    check_error (lh, EINVAL, "lookup bad root_ref");

    /* Lookup dirref with multiple blobrefs, should get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref_multi",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref_multi");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_multi");

    /* Lookup path w/ dirref w/ multiple blobrefs in middle, should
     * get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref_multi.foo",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on dirref_multi, part of path");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_multi, part of path");

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

void lookup_security (void) {
    json_t *root;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * root_ref
     * "val" : val to "foo"
     */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "val", treeobj_create_val ("foo", 3));

    treeobj_hash ("sha1", root, root_ref);

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 5);

    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "val",
                             FLUX_ROLE_OWNER,
                             5,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on val with rolemask owner and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val with rolemask owner and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "val",
                             FLUX_ROLE_USER,
                             5,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create on val with rolemask user and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val with rolemask user and valid owner");
    json_decref (test);

    ok (!lookup_create (cache,
                        krm,
                        1,
                        KVS_PRIMARY_NAMESPACE,
                        NULL,
                        "val",
                        FLUX_ROLE_USER,
                        6,
                        0,
                        NULL,
                        NULL)
        && errno == EPERM,
        "lookup_create on val with rolemask user and invalid owner, returns EPERM");

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup link tests */
void lookup_links (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *dirref3;
    json_t *dir;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t valref_ref;
    blobref_t dirref3_ref;
    blobref_t dirref2_ref;
    blobref_t dirref1_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * dirref3_ref
     * "val" : val to "baz"
     *
     * dirref2_ref
     * "val" : val to "foo"
     * "valref" : valref to valref_ref
     * "dir" : dir w/ "val" : val to "bar"
     * "dirref" : dirref to dirref3_ref
     * "symlink" : symlink to "dirref2.val"
     *
     * dirref1_ref
     * "link2dirref" : symlink to "dirref2"
     * "link2val" : symlink to "dirref2.val"
     * "link2valref" : symlink to "dirref2.valref"
     * "link2dir" : symlink to "dirref2.dir"
     * "link2symlink" : symlink to "dirref2.symlink"
     *
     * root_ref
     * "dirref1" : dirref to "dirref1_ref
     * "dirref2" : dirref to "dirref2_ref
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref);
    cache_insert (cache, valref_ref, create_cache_entry_raw (strdup ("abcd"), 4));

    dirref3 = treeobj_create_dir ();
    treeobj_insert_entry (dirref3, "val", treeobj_create_val ("baz", 3));
    treeobj_hash ("sha1", dirref3, dirref3_ref);
    cache_insert (cache, dirref3_ref, create_cache_entry_treeobj (dirref3));

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("bar", 3));

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref2, "valref", treeobj_create_valref (valref_ref));
    treeobj_insert_entry (dirref2, "dir", dir);
    treeobj_insert_entry (dirref2, "dirref", treeobj_create_dirref (dirref3_ref));
    treeobj_insert_entry (dirref2, "symlink", treeobj_create_symlink ("dirref2.val"));
    treeobj_hash ("sha1", dirref2, dirref2_ref);
    cache_insert (cache, dirref2_ref, create_cache_entry_treeobj (dirref2));

    dirref1 = treeobj_create_dir ();
    treeobj_insert_entry (dirref1, "link2dirref", treeobj_create_symlink ("dirref2"));
    treeobj_insert_entry (dirref1, "link2val", treeobj_create_symlink ("dirref2.val"));
    treeobj_insert_entry (dirref1, "link2valref", treeobj_create_symlink ("dirref2.valref"));
    treeobj_insert_entry (dirref1, "link2dir", treeobj_create_symlink ("dirref2.dir"));
    treeobj_insert_entry (dirref1, "link2symlink", treeobj_create_symlink ("dirref2.symlink"));
    treeobj_hash ("sha1", dirref1, dirref1_ref);
    cache_insert (cache, dirref1_ref, create_cache_entry_treeobj (dirref1));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    treeobj_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 0);

    /* lookup val, follow two links */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to val via two links");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "val via two links");
    json_decref (test);

    /* lookup val, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.link2dirref.val");
    json_decref (test);

    /* lookup valref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.valref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to valref");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.link2dirref.valref");
    json_decref (test);

    /* lookup dir, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.dir",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to dir");
    check_value (lh, dir, "dirref1.link2dirref.dir");

    /* lookup dirref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to dirref");
    check_value (lh, dirref3, "dirref1.link2dirref.dirref");

    /* lookup symlink, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref.symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to symlink");
    test = treeobj_create_symlink ("dirref2.val");
    check_value (lh, test, "dirref1.link2dirref.symlink");
    json_decref (test);

    /* lookup val, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to val (last part path)");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.link2val");
    json_decref (test);

    /* lookup valref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2valref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to valref (last part path)");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.link2valref");
    json_decref (test);

    /* lookup dir, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dir",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to dir (last part path)");
    check_value (lh, dir, "dirref1.link2dir");

    /* lookup dirref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2dirref",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to dirref (last part path)");
    check_value (lh, dirref2, "dirref1.link2dirref");

    /* lookup symlink, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.link2symlink",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READLINK,
                             NULL,
                             NULL)) != NULL,
        "lookup_create link to symlink (last part path)");
    test = treeobj_create_symlink ("dirref2.symlink");
    check_value (lh, test, "dirref1.link2symlink");
    json_decref (test);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup alternate root tests */
void lookup_alt_root (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t dirref1_ref;
    blobref_t dirref2_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * dirref1_ref
     * "val" to "foo"
     *
     * dirref2_ref
     * "val" to "bar"
     *
     * root_ref
     * "dirref1" : dirref to dirref1_ref
     * "dirref2" : dirref to dirref2_ref
     */

    dirref1 = treeobj_create_dir ();
    treeobj_insert_entry (dirref1, "val", treeobj_create_val ("foo", 3));
    treeobj_hash ("sha1", dirref1, dirref1_ref);
    cache_insert (cache, dirref1_ref, create_cache_entry_treeobj (dirref1));

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("bar", 3));
    treeobj_hash ("sha1", dirref2, dirref2_ref);
    cache_insert (cache, dirref2_ref, create_cache_entry_treeobj (dirref2));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    treeobj_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    setup_kvsroot (krm, cache, root_ref, 0);

    /* lookup val, alt root-ref dirref1_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             dirref1_ref,
                             "val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create val w/ dirref1 root_ref");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    /* lookup val, alt root-ref dirref2_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             dirref2_ref,
                             "val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create val w/ dirref2 root_ref");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup stall tests on root */
void lookup_stall_root (void) {
    json_t *root;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * root-ref
     * "val" : val to "foo"
     */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "val", treeobj_create_val ("foo", 3));
    treeobj_hash ("sha1", root, root_ref);

    setup_kvsroot (krm, cache, root_ref, 0);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup root ".", should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest \".\"");
    check_stall (lh, EAGAIN, 1, root_ref, "root \".\" stall");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    /* lookup root ".", should succeed */
    check_value (lh, root, "root \".\" #1");

    /* lookup root ".", now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             ".",
                             FLUX_ROLE_OWNER,
                             0,
                             FLUX_KVS_READDIR,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest \".\"");
    check_value (lh, root, "root \".\" #2");

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

/* lookup stall tests */
void lookup_stall (void) {
    json_t *root;
    json_t *valref_tmp;
    json_t *dirref1;
    json_t *dirref2;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    blobref_t valref1_ref;
    blobref_t valref2_ref;
    blobref_t valref3_ref;
    blobref_t valref4_ref;
    blobref_t valrefmisc1_ref;
    blobref_t valrefmisc2_ref;
    blobref_t dirref1_ref;
    blobref_t dirref2_ref;
    blobref_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((krm = kvsroot_mgr_create (NULL, NULL)) != NULL,
        "kvsroot_mgr_create works");

    /* This cache is
     *
     * valref1_ref
     * "abcd"
     *
     * valref2_ref
     * "efgh"
     *
     * valref3_ref
     * "ijkl"
     *
     * valref4_ref
     * "mnop"
     *
     * valrefmisc1_ref
     * "foobar"
     *
     * valrefmisc2_ref
     * "foobaz"
     *
     * dirref1_ref
     * "val" : val to "foo"
     * "valref" : valref to valref_ref
     * "valrefmisc" : valref to valrefmisc1_ref
     * "valrefmisc_multi" : valref to [ valrefmisc1_ref, valrefmisc2_ref ]
     *
     * dirref2_ref
     * "val" : val to "bar"
     *
     * root_ref
     * "symlink" : symlink to "dirref2"
     * "dirref1" : dirref to dirref1_ref
     * "dirref2" : dirref to dirref2_ref
     *
     */

    blobref_hash ("sha1", "abcd", 4, valref1_ref);
    blobref_hash ("sha1", "efgh", 4, valref2_ref);
    blobref_hash ("sha1", "ijkl", 4, valref3_ref);
    blobref_hash ("sha1", "mnop", 4, valref4_ref);
    blobref_hash ("sha1", "foobar", 4, valrefmisc1_ref);
    blobref_hash ("sha1", "foobaz", 4, valrefmisc2_ref);

    dirref1 = treeobj_create_dir ();
    treeobj_insert_entry (dirref1, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref1, "valref", treeobj_create_valref (valref1_ref));
    valref_tmp = treeobj_create_valref (valref1_ref);
    treeobj_append_blobref (valref_tmp, valref2_ref);
    treeobj_insert_entry (dirref1, "valref_multi", valref_tmp);
    valref_tmp = treeobj_create_valref (valref3_ref);
    treeobj_append_blobref (valref_tmp, valref4_ref);
    treeobj_insert_entry (dirref1, "valref_multi2", valref_tmp);
    treeobj_insert_entry (dirref1, "valrefmisc", treeobj_create_valref (valrefmisc1_ref));
    valref_tmp = treeobj_create_valref (valrefmisc1_ref);
    treeobj_append_blobref (valref_tmp, valrefmisc2_ref);
    treeobj_insert_entry (dirref1, "valrefmisc_multi", valref_tmp);

    treeobj_hash ("sha1", dirref1, dirref1_ref);

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("bar", 3));
    treeobj_hash ("sha1", dirref2, dirref2_ref);

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    treeobj_insert_entry (root, "symlink", treeobj_create_symlink ("dirref2"));
    treeobj_hash ("sha1", root, root_ref);

    setup_kvsroot (krm, cache, root_ref, 0);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup dirref1.val, should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.val");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref1.val stall #1");

    cache_insert (cache, root_ref, create_cache_entry_treeobj (root));

    /* next call to lookup, should stall */
    check_stall (lh, EAGAIN, 1, dirref1_ref, "dirref1.val stall #2");

    cache_insert (cache, dirref1_ref, create_cache_entry_treeobj (dirref1));

    /* final call to lookup, should succeed */
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.val #1");
    json_decref (test);

    /* lookup dirref1.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create dirref1.val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.val #2");
    json_decref (test);

    /* lookup symlink.val, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             root_ref,
                             "symlink.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest symlink.val");
    check_stall (lh, EAGAIN, 1, dirref2_ref, "symlink.val stall");

    cache_insert (cache, dirref2_ref, create_cache_entry_treeobj (dirref2));

    /* lookup symlink.val, should succeed */
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "symlink.val #1");
    json_decref (test);

    /* lookup symlink.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "symlink.val",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create symlink.val");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "symlink.val #2");
    json_decref (test);

    /* lookup dirref1.valref, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    check_stall (lh, EAGAIN, 1, valref1_ref, "dirref1.valref stall");

    cache_insert (cache, valref1_ref, create_cache_entry_raw (strdup ("abcd"), 4));

    /* lookup dirref1.valref, should succeed */
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.valref #1");
    json_decref (test);

    /* lookup dirref1.valref, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.valref #2");
    json_decref (test);

    /* lookup dirref1.valref_multi, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref_multi",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref_multi");
    /* should only be one missing ref, as we loaded one of the refs in
     * the 'valref' above */
    check_stall (lh, EAGAIN, 1, valref2_ref, "dirref1.valref_multi stall");

    cache_insert (cache, valref2_ref, create_cache_entry_raw (strdup ("efgh"), 4));

    /* lookup dirref1.valref_multi, should succeed */
    test = treeobj_create_val ("abcdefgh", 8);
    check_value (lh, test, "dirref1.valref_multi #1");
    json_decref (test);

    /* lookup dirref1.valref_multi, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref_multi",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("abcdefgh", 8);
    check_value (lh, test, "dirref1.valref_multi #2");
    json_decref (test);

    /* lookup dirref1.valref_multi2, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref_multi2",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref_multi2");
    /* should two missing refs, as we have not loaded either here */
    check_stall (lh, EAGAIN, 2, NULL, "dirref1.valref_multi2 stall");

    cache_insert (cache, valref3_ref, create_cache_entry_raw (strdup ("ijkl"), 4));
    cache_insert (cache, valref4_ref, create_cache_entry_raw  (strdup ("mnop"), 4));

    /* lookup dirref1.valref_multi2, should succeed */
    test = treeobj_create_val ("ijklmnop", 8);
    check_value (lh, test, "dirref1.valref_multi2 #1");
    json_decref (test);

    /* lookup dirref1.valref_multi2, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valref_multi2",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("ijklmnop", 8);
    check_value (lh, test, "dirref1.valref_multi2 #2");
    json_decref (test);

    /* lookup dirref1.valrefmisc, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valrefmisc",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valrefmisc");
    /* don't call check_stall, this is primarily to test if callback
     * functions returning errors are caught */
    ok (lookup (lh) == false,
        "dirref1.valrefmisc: lookup stalled");
    errno = 0;
    ok (lookup_iter_missing_refs (lh, lookup_ref_error, NULL) < 0
        && errno == EMLINK,
        "dirref1.valrefmisc: error & errno properly returned from callback error");
    lookup_destroy (lh);

    /* lookup dirref1.valrefmisc_multi, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             1,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             "dirref1.valrefmisc_multi",
                             FLUX_ROLE_OWNER,
                             0,
                             0,
                             NULL,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valrefmisc_multi");
    /* don't call check_stall, this is primarily to test if callback
     * functions returning errors are caught */
    ok (lookup (lh) == false,
        "dirref1.valrefmisc_multi: lookup stalled");
    errno = 0;
    ok (lookup_iter_missing_refs (lh, lookup_ref_error, NULL) < 0
        && errno == EMLINK,
        "dirref1.valrefmisc_multi: error & errno properly returned from callback error");
    lookup_destroy (lh);

    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api ();
    basic_api_errors ();

    lookup_root ();
    lookup_basic ();
    lookup_errors ();
    lookup_security ();
    lookup_links ();
    lookup_alt_root ();
    lookup_stall_root ();
    lookup_stall ();
    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
