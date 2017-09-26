#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/treeobj.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/kvs_util.h"
#include "src/modules/kvs/types.h"
#include "src/common/libutil/base64.h"

void basic_api (void)
{
    struct cache *cache;
    lookup_t *lh;
    const char *tmp;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok ((lh = lookup_create (cache,
                             42,
                             "root.foo",
                             "ref.bar",
                             "path.baz",
                             NULL,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create works");
    ok (lookup_validate (lh) == true,
        "lookup_validate works");
    ok (lookup_get_cache (lh) == cache,
        "lookup_get_cache works");
    ok (lookup_get_current_epoch (lh) == 42,
        "lookup_get_current_epoch works");
    ok ((tmp = lookup_get_root_dir (lh)) != NULL,
        "lookup_get_root_dir works");
    ok (!strcmp (tmp, "root.foo"),
        "lookup_get_root_dir returns correct string");
    ok ((tmp = lookup_get_root_ref (lh)) != NULL,
        "lookup_get_root_ref works");
    ok (!strcmp (tmp, "ref.bar"),
        "lookup_get_root_ref returns correct string");
    ok ((tmp = lookup_get_path (lh)) != NULL,
        "lookup_get_path works");
    ok (!strcmp (tmp, "path.baz"),
        "lookup_get_path returns correct string");
    ok (lookup_get_flags (lh) == (FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ),
        "lookup_get_flags works");
    ok (lookup_set_current_epoch (lh, 43) == 0,
        "lookup_set_current_epoch works");
    ok (lookup_get_current_epoch (lh) == 43,
        "lookup_get_current_epoch works");
    ok (lookup_get_aux_data (lh) == NULL,
        "lookup_get_aux_data returns NULL b/c nothing set");
    ok (lookup_set_aux_data (lh, lh) == 0,
        "lookup_set_aux_data works");
    ok (lookup_get_aux_data (lh) == lh,
        "lookup_get_aux_data returns works");

    lookup_destroy (lh);

    /* if root_ref is set to NULL, make sure both root_dir and
     * root_ref goto root_dir */
    ok ((lh = lookup_create (cache,
                             42,
                             "root.bar",
                             NULL,
                             "path.baz",
                             NULL,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create works");

    ok ((tmp = lookup_get_root_dir (lh)) != NULL,
        "lookup_get_root_dir works");
    ok (!strcmp (tmp, "root.bar"),
        "lookup_get_root_dir returns correct string");
    ok ((tmp = lookup_get_root_ref (lh)) != NULL,
        "lookup_get_root_ref works");
    ok (!strcmp (tmp, "root.bar"),
        "lookup_get_root_ref returns correct string");
    lookup_destroy (lh);

    cache_destroy (cache);
}

void basic_api_errors (void)
{
    struct cache *cache;
    lookup_t *lh;

    ok (lookup_create (NULL,
                       0,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       0) == NULL,
        "lookup_create fails on bad input");

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok ((lh = lookup_create (cache,
                             42,
                             "root.foo",
                             "ref.bar",
                             "path.baz",
                             NULL,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create works");

    ok (lookup_get_errnum (lh) == EINVAL,
        "lookup_get_errnum returns EINVAL b/c lookup not yet started");
    ok (lookup_get_value (lh) == NULL,
        "lookup_get_value fails b/c lookup not yet started");
    ok (lookup_get_missing_ref (lh) == NULL,
        "lookup_get_missing_ref fails b/c lookup not yet started");

    ok (lookup_validate (NULL) == false,
        "lookup_validate fails on NULL pointer");
    ok (lookup (NULL) == true,
        "lookup does not segfault on NULL pointer");
    ok (lookup_get_errnum (NULL) == EINVAL,
        "lookup_get_errnum returns EINVAL on NULL pointer");
    ok (lookup_get_value (NULL) == NULL,
        "lookup_get_value fails on NULL pointer");
    ok (lookup_get_missing_ref (NULL) == NULL,
        "lookup_get_missing_ref fails on NULL pointer");
    ok (lookup_get_cache (NULL) == NULL,
        "lookup_get_cache fails on NULL pointer");
    ok (lookup_get_current_epoch (NULL) < 0,
        "lookup_get_current_epoch fails on NULL pointer");
    ok (lookup_get_root_dir (NULL) == NULL,
        "lookup_get_root_dir fails on NULL pointer");
    ok (lookup_get_root_ref (NULL) == NULL,
        "lookup_get_root_ref fails on NULL pointer");
    ok (lookup_get_path (NULL) == NULL,
        "lookup_get_path fails on NULL pointer");
    ok (lookup_get_flags (NULL) < 0,
        "lookup_get_flags fails on NULL pointer");
    ok (lookup_get_aux_data (NULL) == NULL,
        "lookup_get_aux_data fails on NULL pointer");
    ok (lookup_set_current_epoch (NULL, 42) < 0,
        "lookup_set_current_epoch fails on NULL pointer");
    ok (lookup_set_aux_data (NULL, NULL) < 0,
        "lookup_set_aux_data fails n NULL pointer");
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
    ok (lookup_get_missing_ref (lh) == NULL,
        "lookup_get_missing_ref fails on bad pointer");
    ok (lookup_get_cache (lh) == NULL,
        "lookup_get_cache fails on bad pointer");
    ok (lookup_get_current_epoch (lh) < 0,
        "lookup_get_current_epoch fails on bad pointer");
    ok (lookup_get_root_dir (lh) == NULL,
        "lookup_get_root_dir fails on bad pointer");
    ok (lookup_get_root_ref (lh) == NULL,
        "lookup_get_root_ref fails on bad pointer");
    ok (lookup_get_path (lh) == NULL,
        "lookup_get_path fails on bad pointer");
    ok (lookup_get_flags (lh) < 0,
        "lookup_get_flags fails on bad pointer");
    ok (lookup_get_aux_data (lh) == NULL,
        "lookup_get_aux_data fails on bad pointer");
    ok (lookup_set_current_epoch (lh, 42) < 0,
        "lookup_set_current_epoch fails on bad pointer");
    ok (lookup_set_aux_data (lh, NULL) < 0,
        "lookup_set_aux_data fails n bad pointer");
    /* lookup_destroy ok on bad pointer */
    lookup_destroy (lh);

    cache_destroy (cache);
}

json_t *get_json_base64_string (const char *s)
{
    int len, xlen;
    char *xdata;
    json_t *rv;

    len = strlen (s);
    xlen = base64_encode_length (len);
    xdata = malloc (xlen);
    base64_encode_block (xdata, &xlen, s, len);
    rv = json_string (xdata);
    free (xdata);
    return rv;
}

void check_common (lookup_t *lh,
                   bool lookup_result,
                   int get_errnum_result,
                   json_t *get_value_result,
                   const char *missing_ref_result,
                   const char *msg,
                   bool destroy_lookup)
{
    json_t *val;

    ok (lookup (lh) == lookup_result,
        "%s: lookup matched result", msg);
    ok (lookup_get_errnum (lh) == get_errnum_result,
        "%s: lookup_get_errnum returns expected errnum %d", msg, lookup_get_errnum (lh));
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
    if (missing_ref_result) {
        const char *missing_ref;

        ok ((missing_ref = lookup_get_missing_ref (lh)) != NULL,
            "%s: lookup_get_missing_ref returns expected non-NULL result", msg);

        if (missing_ref) {
            ok (strcmp (missing_ref_result, missing_ref) == 0,
                "%s: missing ref returned matched expectation", msg);
        }
        else {
            ok (false, "%s: missing ref returned matched expectation", msg);
        }
    }
    else {
        ok (lookup_get_missing_ref (lh) == NULL,
            "%s: lookup_get_missing_ref returns NULL as expected", msg);
    }

    if (destroy_lookup)
        lookup_destroy (lh);
}

void check (lookup_t *lh,
            bool lookup_result,
            int get_errnum_result,
            json_t *get_value_result,
            const char *missing_ref_result,
            const char *msg)
{
    check_common (lh,
                  lookup_result,
                  get_errnum_result,
                  get_value_result,
                  missing_ref_result,
                  msg,
                  true);
}

void check_stall (lookup_t *lh,
                  bool lookup_result,
                  int get_errnum_result,
                  json_t *get_value_result,
                  const char *missing_ref_result,
                  const char *msg)
{
    check_common (lh,
                  lookup_result,
                  get_errnum_result,
                  get_value_result,
                  missing_ref_result,
                  msg,
                  false);
}

/* lookup tests on root dir */
void lookup_root (void) {
    json_t *root;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root_ref
     * treeobj dir, no entries
     */

    root = treeobj_create_dir ();
    kvs_util_json_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, cache_entry_create (root));

    /* flags = 0, should error EISDIR */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             ".",
                             NULL,
                             0)) != NULL,
        "lookup_create on root, no flags, works");
    check (lh, true, EISDIR, NULL, NULL, "root no flags");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             ".",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, works");
    check (lh, true, 0, root, NULL, "root w/ FLUX_KVS_READDIR");

    /* flags = FLUX_KVS_TREEOBJ, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             ".",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_TREEOBJ, works");
    test = treeobj_create_dirref (root_ref);
    check (lh, true, 0, test, NULL, "root w/ FLUX_KVS_TREEOBJ");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup basic tests */
void lookup_basic (void) {
    json_t *root;
    json_t *dirref;
    json_t *dir;
    json_t *opaque_data;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;
    href_t valref_ref;
    href_t dirref_ref;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * dirref_ref
     * "valref" : valref to valref_ref
     * "val" : val to "foo"
     * "dir" : dir w/ "val" : val to "bar"
     * "symlink" : symlink to "baz"
     *
     * root_ref
     * "dirref" : dirref to dirref_ref
     */

    opaque_data = get_json_base64_string ("abcd");
    kvs_util_json_hash ("sha1", opaque_data, valref_ref);
    cache_insert (cache, valref_ref, cache_entry_create (opaque_data));

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("bar", 3));

    dirref = treeobj_create_dir ();
    treeobj_insert_entry (dirref, "valref", treeobj_create_valref (valref_ref));
    treeobj_insert_entry (dirref, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref, "dir", dir);
    treeobj_insert_entry (dirref, "symlink", treeobj_create_symlink ("baz"));

    kvs_util_json_hash ("sha1", dirref, dirref_ref);
    cache_insert (cache, dirref_ref, cache_entry_create (dirref));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref", treeobj_create_dirref (dirref_ref));

    kvs_util_json_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, cache_entry_create (root));

    /* lookup dir via dirref */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on path dirref");
    check (lh, true, 0, dirref, NULL, "lookup dirref");

    /* lookup value via valref */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.valref",
                             NULL,
                             0)) != NULL,
        "lookup_create on path dirref.valref");
    test = treeobj_create_val ("abcd", 4);
    check (lh, true, 0, test, NULL, "lookup dirref.valref");
    json_decref (test);

    /* lookup value via val */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.val",
                             NULL,
                             0)) != NULL,
        "lookup_create on path dirref.val");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "lookup dirref.val");
    json_decref (test);

    /* lookup dir via dir */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.dir",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on path dirref.dir");
    check (lh, true, 0, dir, NULL, "lookup dirref.dir");

    /* lookup symlink */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.symlink",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on path dirref.symlink");
    test = treeobj_create_symlink ("baz");
    check (lh, true, 0, test, NULL, "lookup dirref.symlink");
    json_decref (test);

    /* lookup dirref treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dirref (treeobj)");
    test = treeobj_create_dirref (dirref_ref);
    check (lh, true, 0, test, NULL, "lookup dirref treeobj");
    json_decref (test);

    /* lookup valref treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.valref",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dirref.valref (treeobj)");
    test = treeobj_create_valref (valref_ref);
    check (lh, true, 0, test, NULL, "lookup dirref.valref treeobj");
    json_decref (test);

    /* lookup val treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.val",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dirref.val (treeobj)");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "lookup dirref.val treeobj");
    json_decref (test);

    /* lookup dir treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.dir",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dirref.dir (treeobj)");
    check (lh, true, 0, dir, NULL, "lookup dirref.dir treeobj");

    /* lookup symlink treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref.symlink",
                             NULL,
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dirref.symlink (treeobj)");
    test = treeobj_create_symlink ("baz");
    check (lh, true, 0, test, NULL, "lookup dirref.symlink treeobj");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup tests reach an error or "non-good" result */
void lookup_errors (void) {
    json_t *root;
    json_t *dirref;
    json_t *dir;
    json_t *opaque_data;
    json_t *valref_multi;
    json_t *dirref_multi;
    struct cache *cache;
    lookup_t *lh;
    href_t dirref_ref;
    href_t valref_ref;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

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
     * "valref_bad" : valref to dirref_ref
     * "dirref_multi" : dirref to [ dirref_ref, dirref_ref ]
     * "valref_multi" : valref to [ valref_ref, valref_ref ]
     */

    opaque_data = get_json_base64_string ("abcd");
    kvs_util_json_hash ("sha1", opaque_data, valref_ref);
    cache_insert (cache, valref_ref, cache_entry_create (opaque_data));

    dirref = treeobj_create_dir ();
    treeobj_insert_entry (dirref, "val", treeobj_create_val ("bar", 3));
    kvs_util_json_hash ("sha1", dirref, dirref_ref);
    cache_insert (cache, dirref_ref, cache_entry_create (dirref));

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
    treeobj_insert_entry (root, "valref_bad", treeobj_create_valref (dirref_ref));

    valref_multi = treeobj_create_valref (valref_ref);
    treeobj_append_blobref (valref_multi, valref_ref);

    dirref_multi = treeobj_create_dirref (dirref_ref);
    treeobj_append_blobref (dirref_multi, dirref_ref);

    treeobj_insert_entry (root, "dirref_multi", dirref_multi);
    treeobj_insert_entry (root, "valref_multi", valref_multi);

    kvs_util_json_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, cache_entry_create (root));

    /* Lookup non-existent field.  Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on bad path in path");
    check (lh, true, 0, NULL, NULL, "lookup bad path");

    /* Lookup path w/ val in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "val.foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on val in path");
    check (lh, true, 0, NULL, NULL, "lookup val in path");

    /* Lookup path w/ valref in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref.foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on valref in path");
    check (lh, true, 0, NULL, NULL, "lookup valref in path");

    /* Lookup path w/ dir in middle, should get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dir.foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on dir in path");
    check (lh, true, EPERM, NULL, NULL, "lookup dir in path");

    /* Lookup path w/ infinite link loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "symlink1",
                             NULL,
                             0)) != NULL,
        "lookup_create on link loop");
    check (lh, true, ELOOP, NULL, NULL, "lookup infinite links");

    /* Lookup a dirref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on dirref");
    check (lh, true, EINVAL, NULL, NULL, "lookup dirref, expecting link");

    /* Lookup a dir, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dir",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on dir");
    check (lh, true, EINVAL, NULL, NULL, "lookup dir, expecting link");

    /* Lookup a valref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on valref");
    check (lh, true, EINVAL, NULL, NULL, "lookup valref, expecting link");

    /* Lookup a val, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "val",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on val");
    check (lh, true, EINVAL, NULL, NULL, "lookup val, expecting link");

    /* Lookup a dirref, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref",
                             NULL,
                             0)) != NULL,
        "lookup_create on dirref");
    check (lh, true, EISDIR, NULL, NULL, "lookup dirref, not expecting dirref");

    /* Lookup a dir, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dir",
                             NULL,
                             0)) != NULL,
        "lookup_create on dir");
    check (lh, true, EISDIR, NULL, NULL, "lookup dir, not expecting dir");

    /* Lookup a valref, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on valref");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup valref, expecting dir");

    /* Lookup a val, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "val",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on val");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup val, expecting dir");

    /* Lookup a symlink, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "symlink",
                             NULL,
                             FLUX_KVS_READLINK | FLUX_KVS_READDIR)) != NULL,
        "lookup_create on symlink");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup symlink, expecting dir");

    /* Lookup a dirref that doesn't point to a dir, should get EPERM. */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref_bad",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on dirref_bad");
    check (lh, true, EPERM, NULL, NULL, "lookup dirref_bad");

    /* Lookup a valref that doesn't point to a base64 string, should get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref_bad",
                             NULL,
                             0)) != NULL,
        "lookup_create on valref_bad");
    check (lh, true, EPERM, NULL, NULL, "lookup valref_bad");

    /* Lookup with an invalid root_ref, should get EINVAL */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             valref_ref,
                             "val",
                             NULL,
                             0)) != NULL,
        "lookup_create on bad root_ref");
    check (lh, true, EINVAL, NULL, NULL, "lookup bad root_ref");

    /* Lookup dirref with multiple blobrefs, should get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref_multi",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on dirref_multi");
    check (lh, true, EPERM, NULL, NULL, "lookup dirref_multi");

    /* Lookup path w/ dirref w/ multiple blobrefs in middle, should
     * get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref_multi.foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on dirref_multi, part of path");
    check (lh, true, EPERM, NULL, NULL, "lookup dirref_multi, part of path");

    /* Lookup valref with multiple blobrefs, should get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref_multi",
                             NULL,
                             0)) != NULL,
        "lookup_create on valref_multi");
    check (lh, true, EPERM, NULL, NULL, "lookup valref_multi");

    /* Lookup path w/ valref w/ multiple blobrefs in middle, Not
     * ENOENT - caller of lookup decides what to do with entry not
     * found */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "valref_multi.foo",
                             NULL,
                             0)) != NULL,
        "lookup_create on valref_multi, part of path");
    check (lh, true, 0, NULL, NULL, "lookup valref_multi, part of path");

    cache_destroy (cache);
}

/* lookup link tests */
void lookup_links (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *dirref3;
    json_t *dir;
    json_t *opaque_data;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;
    href_t valref_ref;
    href_t dirref3_ref;
    href_t dirref2_ref;
    href_t dirref1_ref;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

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

    opaque_data = get_json_base64_string ("abcd");
    kvs_util_json_hash ("sha1", opaque_data, valref_ref);
    cache_insert (cache, valref_ref, cache_entry_create (opaque_data));

    dirref3 = treeobj_create_dir ();
    treeobj_insert_entry (dirref3, "val", treeobj_create_val ("baz", 3));
    kvs_util_json_hash ("sha1", dirref3, dirref3_ref);
    cache_insert (cache, dirref3_ref, cache_entry_create (dirref3));

    dir = treeobj_create_dir ();
    treeobj_insert_entry (dir, "val", treeobj_create_val ("bar", 3));

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref2, "valref", treeobj_create_valref (valref_ref));
    treeobj_insert_entry (dirref2, "dir", dir);
    treeobj_insert_entry (dirref2, "dirref", treeobj_create_dirref (dirref3_ref));
    treeobj_insert_entry (dirref2, "symlink", treeobj_create_symlink ("dirref2.val"));
    kvs_util_json_hash ("sha1", dirref2, dirref2_ref);
    cache_insert (cache, dirref2_ref, cache_entry_create (dirref2));

    dirref1 = treeobj_create_dir ();
    treeobj_insert_entry (dirref1, "link2dirref", treeobj_create_symlink ("dirref2"));
    treeobj_insert_entry (dirref1, "link2val", treeobj_create_symlink ("dirref2.val"));
    treeobj_insert_entry (dirref1, "link2valref", treeobj_create_symlink ("dirref2.valref"));
    treeobj_insert_entry (dirref1, "link2dir", treeobj_create_symlink ("dirref2.dir"));
    treeobj_insert_entry (dirref1, "link2symlink", treeobj_create_symlink ("dirref2.symlink"));
    kvs_util_json_hash ("sha1", dirref1, dirref1_ref);
    cache_insert (cache, dirref1_ref, cache_entry_create (dirref1));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    kvs_util_json_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, cache_entry_create (root));

    /* lookup val, follow two links */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.symlink",
                             NULL,
                             0)) != NULL,
        "lookup_create link to val via two links");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "val via two links");
    json_decref (test);

    /* lookup val, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.val",
                             NULL,
                             0)) != NULL,
        "lookup_create link to val");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "dirref1.link2dirref.val");
    json_decref (test);

    /* lookup valref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.valref",
                             NULL,
                             0)) != NULL,
        "lookup_create link to valref");
    test = treeobj_create_val ("abcd", 4);
    check (lh, true, 0, test, NULL, "dirref1.link2dirref.valref");
    json_decref (test);

    /* lookup dir, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.dir",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dir");
    check (lh, true, 0, dir, NULL, "dirref1.link2dirref.dir");

    /* lookup dirref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.dirref",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dirref");
    check (lh, true, 0, dirref3, NULL, "dirref1.link2dirref.dirref");

    /* lookup symlink, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref.symlink",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create link to symlink");
    test = treeobj_create_symlink ("dirref2.val");
    check (lh, true, 0, test, NULL, "dirref1.link2dirref.symlink");
    json_decref (test);

    /* lookup val, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2val",
                             NULL,
                             0)) != NULL,
        "lookup_create link to val (last part path)");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "dirref1.link2val");
    json_decref (test);

    /* lookup valref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2valref",
                             NULL,
                             0)) != NULL,
        "lookup_create link to valref (last part path)");
    test = treeobj_create_val ("abcd", 4);
    check (lh, true, 0, test, NULL, "dirref1.link2valref");
    json_decref (test);

    /* lookup dir, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dir",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dir (last part path)");
    check (lh, true, 0, dir, NULL, "dirref1.link2dir");

    /* lookup dirref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2dirref",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dirref (last part path)");
    check (lh, true, 0, dirref2, NULL, "dirref1.link2dirref");

    /* lookup symlink, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.link2symlink",
                             NULL,
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create link to symlink (last part path)");
    test = treeobj_create_symlink ("dirref2.symlink");
    check (lh, true, 0, test, NULL, "dirref1.link2symlink");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup alternate root tests */
void lookup_alt_root (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;
    href_t dirref1_ref;
    href_t dirref2_ref;
    href_t root_ref;
    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

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
    kvs_util_json_hash ("sha1", dirref1, dirref1_ref);
    cache_insert (cache, dirref1_ref, cache_entry_create (dirref1));

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("bar", 3));
    kvs_util_json_hash ("sha1", dirref2, dirref2_ref);
    cache_insert (cache, dirref2_ref, cache_entry_create (dirref2));

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    kvs_util_json_hash ("sha1", root, root_ref);
    cache_insert (cache, root_ref, cache_entry_create (root));

    /* lookup val, alt root-ref dirref1_ref */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             dirref1_ref,
                             "val",
                             NULL,
                             0)) != NULL,
        "lookup_create val w/ dirref1 root_ref");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "alt root val");
    json_decref (test);

    /* lookup val, alt root-ref dirref2_ref */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             dirref2_ref,
                             "val",
                             NULL,
                             0)) != NULL,
        "lookup_create val w/ dirref2 root_ref");
    test = treeobj_create_val ("bar", 3);
    check (lh, true, 0, test, NULL, "alt root val");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup stall tests on root */
void lookup_stall_root (void) {
    json_t *root;
    struct cache *cache;
    lookup_t *lh;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir" : { "DIRREF" : "dir-ref" } }
     */

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "val", treeobj_create_val ("foo", 3));
    kvs_util_json_hash ("sha1", root, root_ref);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup root ".", should stall on root */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             ".",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create stalltest \".\"");
    check_stall (lh, false, EAGAIN, NULL, root_ref, "root \".\" stall");

    cache_insert (cache, root_ref, cache_entry_create (root));

    /* lookup root ".", should succeed */
    check (lh, true, 0, root, NULL, "root \".\" #1");

    /* lookup root ".", now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             ".",
                             NULL,
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create stalltest \".\"");
    check (lh, true, 0, root, NULL, "root \".\" #2");

    cache_destroy (cache);
}

/* lookup stall tests */
void lookup_stall (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *opaque_data;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;
    href_t valref_ref;
    href_t dirref1_ref;
    href_t dirref2_ref;
    href_t root_ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * dirref1_ref
     * "val" : val to "foo"
     * "valref" : valref to valref_ref
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

    opaque_data = get_json_base64_string ("abcd");
    kvs_util_json_hash ("sha1", opaque_data, valref_ref);

    dirref1 = treeobj_create_dir ();
    treeobj_insert_entry (dirref1, "val", treeobj_create_val ("foo", 3));
    treeobj_insert_entry (dirref1, "valref", treeobj_create_valref (valref_ref));
    kvs_util_json_hash ("sha1", dirref1, dirref1_ref);

    dirref2 = treeobj_create_dir ();
    treeobj_insert_entry (dirref2, "val", treeobj_create_val ("bar", 3));
    kvs_util_json_hash ("sha1", dirref2, dirref2_ref);

    root = treeobj_create_dir ();
    treeobj_insert_entry (root, "dirref1", treeobj_create_dirref (dirref1_ref));
    treeobj_insert_entry (root, "dirref2", treeobj_create_dirref (dirref2_ref));
    treeobj_insert_entry (root, "symlink", treeobj_create_symlink ("dirref2"));
    kvs_util_json_hash ("sha1", root, root_ref);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup dirref1.val, should stall on root */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.val",
                             NULL,
                             0)) != NULL,
        "lookup_create stalltest dirref1.val");
    check_stall (lh, false, EAGAIN, NULL, root_ref, "dirref1.val stall #1");

    cache_insert (cache, root_ref, cache_entry_create (root));

    /* next call to lookup, should stall */
    check_stall (lh, false, EAGAIN, NULL, dirref1_ref, "dirref1.val stall #2");

    cache_insert (cache, dirref1_ref, cache_entry_create (dirref1));

    /* final call to lookup, should succeed */
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "dirref1.val #1");
    json_decref (test);

    /* lookup dirref1.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.val",
                             NULL,
                             0)) != NULL,
        "lookup_create dirref1.val");
    test = treeobj_create_val ("foo", 3);
    check (lh, true, 0, test, NULL, "dirref1.val #2");
    json_decref (test);

    /* lookup symlink.val, should stall */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "symlink.val",
                             NULL,
                             0)) != NULL,
        "lookup_create stalltest symlink.val");
    check_stall (lh, false, EAGAIN, NULL, dirref2_ref, "symlink.val stall");

    cache_insert (cache, dirref2_ref, cache_entry_create (dirref2));

    /* lookup symlink.val, should succeed */
    test = treeobj_create_val ("bar", 3);
    check (lh, true, 0, test, NULL, "symlink.val #1");
    json_decref (test);

    /* lookup symlink.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "symlink.val",
                             NULL,
                             0)) != NULL,
        "lookup_create symlink.val");
    test = treeobj_create_val ("bar", 3);
    check (lh, true, 0, test, NULL, "symlink.val #2");
    json_decref (test);

    /* lookup dirref1.valref, should stall */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.valref",
                             NULL,
                             0)) != NULL,
        "lookup_create stalltest dirref1.valref");
    check_stall (lh, false, EAGAIN, NULL, valref_ref, "dirref1.valref stall");

    cache_insert (cache, valref_ref, cache_entry_create (opaque_data));

    /* lookup dirref1.valref, should succeed */
    test = treeobj_create_val ("abcd", 4);
    check (lh, true, 0, test, NULL, "dirref1.valref #1");
    json_decref (test);

    /* lookup dirref1.valref, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             root_ref,
                             root_ref,
                             "dirref1.valref",
                             NULL,
                             0)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("abcd", 4);
    check (lh, true, 0, test, NULL, "dirref1.valref #2");
    json_decref (test);

    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api ();
    basic_api_errors ();

    lookup_root ();
    lookup_basic ();
    lookup_errors ();
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
