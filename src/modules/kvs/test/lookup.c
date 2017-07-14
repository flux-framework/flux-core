#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/jansson_dirent.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/lookup.h"
#include "src/modules/kvs/json_util.h"

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
                       0) == NULL,
        "lookup_create fails on bad input");

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok ((lh = lookup_create (cache,
                             42,
                             "root.foo",
                             "ref.bar",
                             "path.baz",
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
        "%s: lookup_get_errnum returns expected errnum", msg);
    if (get_value_result) {
        ok ((val = lookup_get_value (lh)) != NULL,
            "%s: lookup_get_value returns non-NULL as expected", msg);
        if (val) {
            ok (json_compare (get_value_result, val) == true,
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

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir" : { "DIRREF" : "dir-ref" } }
     */

    root = json_object ();
    json_object_set_new (root, "dir", j_dirent_create ("DIRREF", "dir-ref"));
    cache_insert (cache, "root-ref", cache_entry_create (root));

    /* flags = 0, should error EISDIR */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             ".",
                             0)) != NULL,
        "lookup_create on root, no flags, works");
    check (lh, true, EISDIR, NULL, NULL, "root no flags");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             ".",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, works");
    check (lh, true, 0, root, NULL, "root w/ FLUX_KVS_READDIR");

    /* flags = FLUX_KVS_TREEOBJ, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             ".",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_TREEOBJ, works");
    test = j_dirent_create ("DIRREF", "root-ref");
    check (lh, true, 0, test, NULL, "root w/ FLUX_KVS_TREEOBJ");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup basic tests */
void lookup_basic (void) {
    json_t *root;
    json_t *dirref;
    json_t *dirval;
    json_t *linkval;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir" : { "DIRREF" : "dir-ref" } }
     *
     * dir-ref
     * { "fileval" : { "FILEVAL" : 42 }
     *   "file" : { "FILEREF" : "file-ref" }
     *   "dirval" : { "DIRVAL" : { "foo" : { "FILEVAL" : 43 } } }
     *   "linkval" : { "LINKVAL" : "baz" } }
     *
     * file-ref
     * { 44 }
     */

    root = json_object ();
    json_object_set_new (root, "dir", j_dirent_create ("DIRREF", "dir-ref"));
    cache_insert (cache, "root-ref", cache_entry_create (root));

    dirval = json_object ();
    json_object_set_new (dirval, "foo", j_dirent_create ("FILEVAL", json_integer (43)));

    linkval = j_dirent_create ("LINKVAL", json_string ("baz"));

    dirref = json_object ();
    json_object_set_new (dirref, "fileval", j_dirent_create ("FILEVAL", json_integer (42)));
    json_object_set_new (dirref, "file", j_dirent_create ("FILEREF", "file-ref"));
    json_object_set_new (dirref, "dirval", j_dirent_create ("DIRVAL", dirval));
    json_object_set_new (dirref, "linkval", linkval);

    cache_insert (cache, "dir-ref", cache_entry_create (dirref));

    cache_insert (cache, "file-ref", cache_entry_create (json_integer (44)));

    /* lookup dir value */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on path dir");
    check (lh, true, 0, dirref, NULL, "lookup dir");

    /* lookup file value */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.file",
                             0)) != NULL,
        "lookup_create on path dir.file");
    test = json_integer (44);
    check (lh, true, 0, test, NULL, "lookup dir.file");
    json_decref (test);

    /* lookup fileval value */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.fileval",
                             0)) != NULL,
        "lookup_create on path dir.fileval");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "lookup dir.fileval");
    json_decref (test);

    /* lookup dirval value */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.dirval",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on path dir.dirval");
    check (lh, true, 0, dirval, NULL, "lookup dir.dirval");

    /* lookup linkval value */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.linkval",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on path dir.linkval");
    test = json_string ("baz");
    check (lh, true, 0, test, NULL, "lookup dir.linkval");
    json_decref (test);

    /* lookup dir treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dir (treeobj)");
    test = j_dirent_create ("DIRREF", "dir-ref");
    check (lh, true, 0, test, NULL, "lookup dir treeobj");
    json_decref (test);

    /* lookup file treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.file",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dir.file (treeobj)");
    test = j_dirent_create ("FILEREF", "file-ref");
    check (lh, true, 0, test, NULL, "lookup dir.file treeobj");
    json_decref (test);

    /* lookup fileval treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.fileval",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dir.fileval (treeobj)");
    test = j_dirent_create ("FILEVAL", json_integer (42));
    check (lh, true, 0, test, NULL, "lookup dir.fileval treeobj");
    json_decref (test);

    /* lookup dirval treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.dirval",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dir.dirval (treeobj)");
    test = j_dirent_create ("DIRVAL", dirval);
    check (lh, true, 0, test, NULL, "lookup dir.dirval treeobj");
    json_decref (test);

    /* lookup linkval treeobj */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir.linkval",
                             FLUX_KVS_TREEOBJ)) != NULL,
        "lookup_create on path dir.linkval (treeobj)");
    check (lh, true, 0, linkval, NULL, "lookup dir.linkval treeobj");

    cache_destroy (cache);
}

/* lookup tests reach an error or "non-good" result */
void lookup_errors (void) {
    json_t *root;
    json_t *dirval;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dirref" : { "DIRREF" : "dirref-ref" },
     *   "fileref" : { "FILEREF" : "fileref-ref" }
     *   "dirval" : { "DIRVAL" : { "foo" : { "FILEVAL" : 42 } } }
     *   "fileval" : { "FILEVAL" : 42 }
     *   "linkval" : { "LINKVAL" : "linkvalstr" }
     *   "linkval1" : { "LINKVAL" : "linkval2" }
     *   "linkval2" : { "LINKVAL" : "linkval1" } }
     */

    dirval = json_object ();
    json_object_set_new (dirval, "foo", j_dirent_create ("FILEVAL", json_integer (42)));

    root = json_object ();
    json_object_set_new (root, "dirref", j_dirent_create ("DIRREF", "dirref-ref"));
    json_object_set_new (root, "fileref", j_dirent_create ("FILEREF", "fileref-ref"));
    json_object_set_new (root, "dirval", j_dirent_create ("DIRVAL", dirval));
    json_object_set_new (root, "fileval", j_dirent_create ("FILEVAL", json_integer (42)));
    json_object_set_new (root, "linkval", j_dirent_create ("LINKVAL", json_string ("linkvalstr")));
    json_object_set_new (root, "linkval1", j_dirent_create ("LINKVAL", json_string ("linkval2")));
    json_object_set_new (root, "linkval2", j_dirent_create ("LINKVAL", json_string ("linkval1")));

    cache_insert (cache, "root-ref", cache_entry_create (root));

    /* Lookup non-existent field.  Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "foo",
                             0)) != NULL,
        "lookup_create on bad path in path");
    check (lh, true, 0, NULL, NULL, "lookup bad path");

    /* Lookup path w/ fileval in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileval.foo",
                             0)) != NULL,
        "lookup_create on fileval in path");
    check (lh, true, 0, NULL, NULL, "lookup fileval in path");

    /* Lookup path w/ fileref in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileref.foo",
                             0)) != NULL,
        "lookup_create on fileref in path");
    check (lh, true, 0, NULL, NULL, "lookup fileref in path");

    /* Lookup path w/ dirval in middle, should get EPERM */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dirval.foo",
                             0)) != NULL,
        "lookup_create on dirval in path");
    check (lh, true, EPERM, NULL, NULL, "lookup dirval in path");

    /* Lookup path w/ infinite link loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "linkval1",
                             0)) != NULL,
        "lookup_create on link loop");
    check (lh, true, ELOOP, NULL, NULL, "lookup infinite links");

    /* Lookup a dirref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dirref",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on dirref");
    check (lh, true, EINVAL, NULL, NULL, "lookup dirref, expecting link");

    /* Lookup a dirval, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dirval",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on dirval");
    check (lh, true, EINVAL, NULL, NULL, "lookup dirval, expecting link");

    /* Lookup a fileref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileref",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on fileref");
    check (lh, true, EINVAL, NULL, NULL, "lookup fileref, expecting link");

    /* Lookup a fileval, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileval",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create on fileval");
    check (lh, true, EINVAL, NULL, NULL, "lookup fileval, expecting link");

    /* Lookup a dirref, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dirref",
                             0)) != NULL,
        "lookup_create on dirref");
    check (lh, true, EISDIR, NULL, NULL, "lookup dirref, not expecting dirref");

    /* Lookup a dirval, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dirval",
                             0)) != NULL,
        "lookup_create on dirval");
    check (lh, true, EISDIR, NULL, NULL, "lookup dirval, not expecting dirval");

    /* Lookup a fileref, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileref",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on fileref");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup fileref, expecting dir");

    /* Lookup a fileval, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "fileval",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create on fileval");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup fileval, expecting dir");

    /* Lookup a linkval, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "linkval",
                             FLUX_KVS_READLINK | FLUX_KVS_READDIR)) != NULL,
        "lookup_create on linkval");
    check (lh, true, ENOTDIR, NULL, NULL, "lookup linkval, expecting dir");

    cache_destroy (cache);
}

/* lookup link tests */
void lookup_links (void) {
    json_t *root;
    json_t *dir1ref;
    json_t *dir2ref;
    json_t *dir3ref;
    json_t *dirval;
    json_t *linkval;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir1" : { "DIRREF" : "dir1-ref" }
     *   "dir2" : { "DIRREF" : "dir2-ref" } }
     *
     * dir1-ref
     * { "link2dir" : { "LINKVAL" : "dir2" }
     *   "link2fileval" : { "LINKVAL" : "dir2.fileval" }
     *   "link2file" : { "LINKVAL" : "dir2.file" }
     *   "link2dirval" : { "LINKVAL" : "dir2.dirval" }
     *   "link2linkval" : { "LINKVAL" : "dir2.linkval" } }
     *
     * dir2-ref
     * { "fileval" : { "FILEVAL" : 42 }
     *   "file" : { "FILEREF" : "file-ref" }
     *   "dirval" : { "DIRVAL" : { "foo" : { "FILEVAL" : 43 } } }
     *   "dir" : { "DIRREF" : "dir3-ref" }
     *   "linkval" : { "LINKVAL" : "dir2.fileval" } }
     *
     * dir3-ref
     * { "fileval" : { "FILEVAL" : 44 } }
     *
     * file-ref
     * { 45 }
     */

    root = json_object ();
    json_object_set_new (root, "dir1", j_dirent_create ("DIRREF", "dir1-ref"));
    json_object_set_new (root, "dir2", j_dirent_create ("DIRREF", "dir2-ref"));
    cache_insert (cache, "root-ref", cache_entry_create (root));

    dir1ref = json_object ();
    json_object_set_new (dir1ref, "link2dir", j_dirent_create ("LINKVAL", json_string ("dir2")));
    json_object_set_new (dir1ref, "link2fileval", j_dirent_create ("LINKVAL", json_string ("dir2.fileval")));
    json_object_set_new (dir1ref, "link2file", j_dirent_create ("LINKVAL", json_string ("dir2.file")));
    json_object_set_new (dir1ref, "link2dirval", j_dirent_create ("LINKVAL", json_string ("dir2.dirval")));
    json_object_set_new (dir1ref, "link2linkval", j_dirent_create ("LINKVAL", json_string ("dir2.linkval")));

    cache_insert (cache, "dir1-ref", cache_entry_create (dir1ref));

    dirval = json_object ();
    json_object_set_new (dirval, "foo", j_dirent_create ("FILEVAL", json_integer (43)));

    linkval = j_dirent_create ("LINKVAL", json_string ("dir2.fileval"));

    dir2ref = json_object ();
    json_object_set_new (dir2ref, "fileval", j_dirent_create ("FILEVAL", json_integer (42)));
    json_object_set_new (dir2ref, "file", j_dirent_create ("FILEREF", "file-ref"));
    json_object_set_new (dir2ref, "dirval", j_dirent_create ("DIRVAL", dirval));
    json_object_set_new (dir2ref, "dir", j_dirent_create ("DIRREF", "dir3-ref"));
    json_object_set_new (dir2ref, "linkval", linkval);

    cache_insert (cache, "dir2-ref", cache_entry_create (dir2ref));

    dir3ref = json_object ();
    json_object_set_new (dir3ref, "fileval", j_dirent_create ("FILEVAL", json_integer (44)));

    cache_insert (cache, "dir3-ref", cache_entry_create (dir3ref));

    cache_insert (cache, "file-ref", cache_entry_create (json_integer (45)));

    /* lookup fileval, follow two links */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.linkval",
                             0)) != NULL,
        "lookup_create link to fileval via two links");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "fileval via two links");
    json_decref (test);

    /* lookup fileval, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.fileval",
                             0)) != NULL,
        "lookup_create link to fileval");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "dir1.link2dir.fileval");
    json_decref (test);

    /* lookup file, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.file",
                             0)) != NULL,
        "lookup_create link to file");
    test = json_integer (45);
    check (lh, true, 0, test, NULL, "dir1.link2dir.file");
    json_decref (test);

    /* lookup dirval, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.dirval",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dirval");
    check (lh, true, 0, dirval, NULL, "dir1.link2dir.dirval");

    /* lookup dir, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.dir",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dir");
    check (lh, true, 0, dir3ref, NULL, "dir1.link2dir.dir");

    /* lookup linkval, link is middle of path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir.linkval",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create link to linkval");
    test = json_string ("dir2.fileval");
    check (lh, true, 0, test, NULL, "dir1.link2dir.linkval");
    json_decref (test);

    /* lookup fileval, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2fileval",
                             0)) != NULL,
        "lookup_create link to fileval (last part path)");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "dir1.link2fileval");
    json_decref (test);

    /* lookup file, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2file",
                             0)) != NULL,
        "lookup_create link to file (last part path)");
    test = json_integer (45);
    check (lh, true, 0, test, NULL, "dir1.link2file");
    json_decref (test);

    /* lookup dirval, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dirval",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dirval (last part path)");
    check (lh, true, 0, dirval, NULL, "dir1.link2dirval");

    /* lookup dir, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2dir",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create link to dir (last part path)");
    check (lh, true, 0, dir2ref, NULL, "dir1.link2dir");

    /* lookup linkval, link is last part in path */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.link2linkval",
                             FLUX_KVS_READLINK)) != NULL,
        "lookup_create link to linkval (last part path)");
    test = json_string ("dir2.linkval");
    check (lh, true, 0, test, NULL, "dir1.link2linkval");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup alternate root tests */
void lookup_alt_root (void) {
    json_t *root;
    json_t *dir1ref;
    json_t *dir2ref;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir1" : { "DIRREF" : "dir1-ref" }
     *   "dir2" : { "DIRREF" : "dir2-ref" } }
     *
     * dir1-ref
     * { "fileval" : { "FILEVAL" : 42 } }
     *
     * dir2-ref
     * { "fileval" : { "FILEVAL" : 43 } }
     */

    root = json_object ();
    json_object_set_new (root, "dir1", j_dirent_create ("DIRREF", "dir1-ref"));
    json_object_set_new (root, "dir2", j_dirent_create ("DIRREF", "dir2-ref"));
    cache_insert (cache, "root-ref", cache_entry_create (root));

    dir1ref = json_object ();
    json_object_set_new (dir1ref, "fileval", j_dirent_create ("FILEVAL", json_integer (42)));
    cache_insert (cache, "dir1-ref", cache_entry_create (dir1ref));

    dir2ref = json_object ();
    json_object_set_new (dir2ref, "fileval", j_dirent_create ("FILEVAL", json_integer (43)));
    cache_insert (cache, "dir2-ref", cache_entry_create (dir2ref));

    /* lookup fileval, alt root-ref dir1-ref */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "dir1-ref",
                             "fileval",
                             0)) != NULL,
        "lookup_create fileval w/ dir1ref root_ref");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "alt root fileval");
    json_decref (test);

    /* lookup fileval, alt root-ref dir2-ref */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "dir2-ref",
                             "fileval",
                             0)) != NULL,
        "lookup_create fileval w/ dir2ref root_ref");
    test = json_integer (43);
    check (lh, true, 0, test, NULL, "alt root fileval");
    json_decref (test);

    cache_destroy (cache);
}

/* lookup stall tests on root */
void lookup_stall_root (void) {
    json_t *root;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir" : { "DIRREF" : "dir-ref" } }
     */

    root = json_object ();
    json_object_set_new (root, "dir", j_dirent_create ("DIRREF", "dir-ref"));

    /* do not insert entries into cache until later for these stall tests */

    /* lookup root ".", should stall on root */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             ".",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create stalltest \".\"");
    check_stall (lh, false, EAGAIN, NULL, "root-ref", "root \".\" stall");

    cache_insert (cache, "root-ref", cache_entry_create (root));

    /* lookup root ".", should succeed */
    check (lh, true, 0, root, NULL, "root \".\" #1");

    /* lookup root ".", now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             ".",
                             FLUX_KVS_READDIR)) != NULL,
        "lookup_create stalltest \".\"");
    check (lh, true, 0, root, NULL, "root \".\" #2");

    cache_destroy (cache);
}

/* lookup stall tests */
void lookup_stall (void) {
    json_t *root;
    json_t *dir1ref;
    json_t *dir2ref;
    json_t *fileref;
    json_t *test;
    struct cache *cache;
    lookup_t *lh;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    /* This cache is
     *
     * root-ref
     * { "dir1" : { "DIRREF" : "dir1-ref" }
     *   "dir2" : { "DIRREF" : "dir2-ref" }
     *   "linkval" : { "LINKVAL" : "dir2" } }
     *
     * dir1-ref
     * { "fileval" : { "FILEVAL" : 42 }
     *   "file" : { "FILEREF" : "file-ref" } }
     *
     * dir2-ref
     * { "fileval" : { "FILEVAL" : 43 } }
     *
     * file-ref
     * { 44 }
     *
     */

    root = json_object ();
    json_object_set_new (root, "dir1", j_dirent_create ("DIRREF", "dir1-ref"));
    json_object_set_new (root, "dir2", j_dirent_create ("DIRREF", "dir2-ref"));
    json_object_set_new (root, "linkval", j_dirent_create ("LINKVAL", json_string ("dir2")));

    dir1ref = json_object ();
    json_object_set_new (dir1ref, "fileval", j_dirent_create ("FILEVAL", json_integer (42)));
    json_object_set_new (dir1ref, "file", j_dirent_create ("FILEREF", "file-ref"));

    dir2ref = json_object ();
    json_object_set_new (dir2ref, "fileval", j_dirent_create ("FILEVAL", json_integer (43)));

    fileref = json_integer (44);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup dir1.fileval, should stall on root */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.fileval",
                             0)) != NULL,
        "lookup_create stalltest dir1.fileval");
    check_stall (lh, false, EAGAIN, NULL, "root-ref", "dir1.fileval stall #1");

    cache_insert (cache, "root-ref", cache_entry_create (root));

    /* next call to lookup, should stall */
    check_stall (lh, false, EAGAIN, NULL, "dir1-ref", "dir1.fileval stall #2");

    cache_insert (cache, "dir1-ref", cache_entry_create (dir1ref));

    /* final call to lookup, should succeed */
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "dir1.fileval #1");
    json_decref (test);

    /* lookup dir1.fileval, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.fileval",
                             0)) != NULL,
        "lookup_create dir1.fileval");
    test = json_integer (42);
    check (lh, true, 0, test, NULL, "dir1.fileval #2");
    json_decref (test);

    /* lookup linkval.fileval, should stall */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "linkval.fileval",
                             0)) != NULL,
        "lookup_create stalltest linkval.fileval");
    check_stall (lh, false, EAGAIN, NULL, "dir2-ref", "linkval.fileval stall");

    cache_insert (cache, "dir2-ref", cache_entry_create (dir2ref));

    /* lookup linkval.fileval, should succeed */
    test = json_integer (43);
    check (lh, true, 0, test, NULL, "linkval.fileval #1");
    json_decref (test);

    /* lookup linkval.fileval, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "linkval.fileval",
                             0)) != NULL,
        "lookup_create linkval.fileval");
    test = json_integer (43);
    check (lh, true, 0, test, NULL, "linkval.fileval #2");
    json_decref (test);

    /* lookup dir1.file, should stall */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.file",
                             0)) != NULL,
        "lookup_create stalltest dir1.file");
    check_stall (lh, false, EAGAIN, NULL, "file-ref", "dir1.file stall");

    cache_insert (cache, "file-ref", cache_entry_create (fileref));

    /* lookup dir1.file, should succeed */
    test = json_integer (44);
    check (lh, true, 0, test, NULL, "dir1.file #1");
    json_decref (test);

    /* lookup dir1.file, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             1,
                             "root-ref",
                             "root-ref",
                             "dir1.file",
                             0)) != NULL,
        "lookup_create stalltest dir1.file");
    test = json_integer (44);
    check (lh, true, 0, test, NULL, "dir1.file #2");
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
