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
#include <limits.h>
#include <jansson.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libtap/tap.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_util_private.h"
#include "src/modules/kvs/cache.h"
#include "src/modules/kvs/lookup.h"
#include "src/common/libutil/blobref.h"
#include "ccan/str/str.h"

struct flux_msg_cred owner_cred = { .userid = 0, .rolemask = FLUX_ROLE_OWNER };
struct flux_msg_cred user_cred = { .userid = 0, .rolemask = FLUX_ROLE_USER };

struct lookup_ref_data
{
    const char *ref;
    int count;
};

static void ltest_finalize (struct cache *cache, kvsroot_mgr_t *krm)
{
    cache_destroy (cache);
    kvsroot_mgr_destroy (krm);
}

static void ltest_init (struct cache **cache, kvsroot_mgr_t **krm)
{
    if (!(*cache = cache_create (NULL)))
        BAIL_OUT ("cache_create failed");
    if (!(*krm = kvsroot_mgr_create (NULL, NULL)))
        BAIL_OUT ("kvsroot_mgr_create failed");
};

static int treeobj_hash (const char *hash_name, json_t *obj,
                         char *blobref, int blobref_len)
{
    char *tmp = NULL;
    int rc = -1;

    if (!hash_name
        || !obj
        || !blobref
        || blobref_len < BLOBREF_MAX_STRING_SIZE) {
        errno = EINVAL;
        goto error;
    }

    if (treeobj_validate (obj) < 0)
        goto error;

    if (!(tmp = treeobj_encode (obj)))
        goto error;

    if (blobref_hash (hash_name, (uint8_t *)tmp, strlen (tmp), blobref,
                      blobref_len) < 0)
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
static struct cache_entry *create_cache_entry_raw (const char *ref,
                                                   void *data,
                                                   int len)
{
    struct cache_entry *entry;
    int ret;

    assert (data);
    assert (len);

    entry = cache_entry_create (ref);
    assert (entry);
    ret = cache_entry_set_raw (entry, data, len);
    assert (ret == 0);
    return entry;
}

/* convenience function */
static struct cache_entry *create_cache_entry_treeobj (const char *ref,
                                                       json_t *o)
{
    struct cache_entry *entry;
    int ret;

    assert (o);

    entry = cache_entry_create (ref);
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
                    const char *ns,
                    struct cache *cache,
                    const char *ref,
                    uint32_t owner)
{
    struct kvsroot *root;

    ok ((root = kvsroot_mgr_create_root (krm,
                                         cache,
                                         "sha1",
                                         ns,
                                         owner,
                                         0)) != NULL,
        "kvsroot_mgr_create_root works");

    kvsroot_setroot (krm, root, ref, 0);
}

/* wraps treeobj_create_val() and treeobj_insert_entry(),
 * so created val can be properly dereferenced
 */
void _treeobj_insert_entry_val (json_t *obj, const char *name,
                                const void *data, int len)
{
    json_t *val = treeobj_create_val (data, len);
    treeobj_insert_entry (obj, name, val);
    json_decref (val);
}

/* wraps treeobj_create_symlink() and treeobj_insert_entry(), so
 * created symlink can be properly dereferenced
 */
void _treeobj_insert_entry_symlink (json_t *obj, const char *name,
                                    const char *ns, const char *target)
{
    json_t *symlink = treeobj_create_symlink (ns, target);
    treeobj_insert_entry (obj, name, symlink);
    json_decref (symlink);
}

/* wraps treeobj_create_valref() and treeobj_insert_entry(), so
 * created valref can be properly dereferenced
 */
void _treeobj_insert_entry_valref (json_t *obj, const char *name,
                                   const char *blobref)
{
    json_t *valref = treeobj_create_valref (blobref);
    treeobj_insert_entry (obj, name, valref);
    json_decref (valref);
}

/* wraps treeobj_create_dirref() and treeobj_insert_entry(), so
 * created dirref can be properly dereferenced
 */
void _treeobj_insert_entry_dirref (json_t *obj, const char *name,
                                   const char *blobref)
{
    json_t *dirref = treeobj_create_dirref (blobref);
    treeobj_insert_entry (obj, name, dirref);
    json_decref (dirref);
}

void basic_api (void)
{
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    const char *tmp;

    ltest_init (&cache, &krm);

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, "root.ref.foo", 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             "root.ref.foo",
                             0,
                             "path.bar",
                             owner_cred,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create works");
    ok ((tmp = lookup_get_namespace (lh)) != NULL,
        "lookup_get_namespace works");
    ok (streq (tmp, KVS_PRIMARY_NAMESPACE),
        "lookup_get_namespace returns correct string");
    ok (lookup_missing_namespace (lh) == NULL,
        "lookup_missing_namespace returned NULL, no missing namespace yet");
    ok (lookup_get_aux_errnum (lh) == 0,
        "lookup_get_aux_errnum returns no error");
    ok (lookup_set_aux_errnum (lh, EINVAL) == EINVAL,
        "lookup_set_aux_errnum works");
    ok (lookup_get_aux_errnum (lh) == EINVAL,
        "lookup_get_aux_errnum gets EINVAL");
    ok (lookup_get_root_ref (lh) == NULL,
        "lookup_get_root_ref fails on not-completed lookup");
    ok (lookup_get_root_seq (lh) < 0,
        "lookup_get_root_seq fails on not-completed lookup");

    lookup_destroy (lh);

    ltest_finalize (cache, krm);
}

void basic_api_errors (void)
{
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;

    ok (lookup_create (NULL,
                       NULL,
                       NULL,
                       NULL,
                       0,
                       NULL,
                       owner_cred,
                       0,
                       NULL) == NULL,
        "lookup_create fails on bad input");

    ltest_init (&cache, &krm);

    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             NULL,
                             0,
                             "path.bar",
                             owner_cred,
                             0,
                             NULL)) == NULL,
        "lookup_create fails on no namespace or root ref");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, "root.ref.foo", 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "path.bar",
                             owner_cred,
                             FLUX_KVS_READLINK | FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create works");

    ok (lookup_get_errnum (lh) == EINVAL,
        "lookup_get_errnum returns EINVAL b/c lookup not yet started");
    ok (lookup_get_value (lh) == NULL,
        "lookup_get_value fails b/c lookup not yet started");
    ok (lookup_iter_missing_refs (lh, lookup_ref, NULL) < 0,
        "lookup_iter_missing_refs fails b/c lookup not yet started");

    ok (lookup (NULL) == LOOKUP_PROCESS_ERROR,
        "lookup does not segfault on NULL pointer");
    ok (lookup_get_errnum (NULL) == EINVAL,
        "lookup_get_errnum returns EINVAL on NULL pointer");
    ok (lookup_get_value (NULL) == NULL,
        "lookup_get_value fails on NULL pointer");
    ok (lookup_iter_missing_refs (NULL, lookup_ref, NULL) < 0,
        "lookup_iter_missing_refs fails on NULL pointer");
    ok (lookup_missing_namespace (NULL) == NULL,
        "lookup_missing_namespace fails on NULL pointer");
    ok (lookup_get_namespace (NULL) == NULL,
        "lookup_get_namespace fails on NULL pointer");
    ok (lookup_get_root_ref (NULL) == NULL,
        "lookup_get_root_ref fails on NULL pointer");
    ok (lookup_get_root_seq (NULL) < 0,
        "lookup_get_root_seq fails on NULL pointer");
    /* lookup_destroy ok on NULL pointer */
    lookup_destroy (NULL);

    lookup_destroy (lh);

    ltest_finalize (cache, krm);
}

/* basic lookup to test a few situations that we don't want to
 * replicate in all the main tests below
 */
void basic_lookup (void) {
    json_t *root;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_ref[BLOBREF_MAX_STRING_SIZE];
    const char *tmp;

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root_ref
     * "val" : val to "foo"
     */

    root = treeobj_create_dir ();
    _treeobj_insert_entry_val (root, "val", "foo", 3);

    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create basic works - no root_ref set");
    ok (lookup (lh) == LOOKUP_PROCESS_FINISHED,
        "lookup process finished");
    ok ((tmp = lookup_get_root_ref (lh)) != NULL,
        "lookup_get_root_ref returns non-NULL");
    ok (streq (tmp, root_ref),
        "lookup_get_root_ref returned correct root_ref");
    ok (lookup_get_root_seq (lh) >= 0,
        "lookup_get_root_seq returned valid root_seq");

    lookup_destroy (lh);

    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             18,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create basic works - root_ref set");
    ok (lookup (lh) == LOOKUP_PROCESS_FINISHED,
        "lookup process finished");
    ok ((tmp = lookup_get_root_ref (lh)) != NULL,
        "lookup_get_root_ref returns non-NULL");
    ok (streq (tmp, root_ref),
        "lookup_get_root_ref returned correct root_ref");
    ok (lookup_get_root_seq (lh) == 18,
        "lookup_get_root_seq returned correct root_seq");

    lookup_destroy (lh);

    ltest_finalize (cache, krm);
    json_decref (root);
}

void check_common (lookup_t *lh,
                   lookup_process_t lookup_result,
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
                ok (streq (ld.ref, missing_ref_result),
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
                  LOOKUP_PROCESS_FINISHED,
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
                  LOOKUP_PROCESS_FINISHED,
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
                  LOOKUP_PROCESS_LOAD_MISSING_REFS,
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
                  LOOKUP_PROCESS_ERROR,
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
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * root_ref
     * treeobj dir, no entries
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    root = treeobj_create_dir ();
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* flags = 0, should error EISDIR */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on root, no flags, works");
    check_error (lh, EISDIR, "root no flags");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, works");
    check_value (lh, root, "root w/ FLUX_KVS_READDIR");

    /* flags = FLUX_KVS_TREEOBJ, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_TREEOBJ, works");
    test = treeobj_create_dirref (root_ref);
    check_value (lh, test, "root w/ FLUX_KVS_TREEOBJ");
    json_decref (test);

    /* flags = FLUX_KVS_READDIR, bad root_ref, should error EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             valref_ref,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on root w/ flag = FLUX_KVS_READDIR, bad root_ref, should EINVAL");
    check_error (lh, EINVAL, "root w/ FLUX_KVS_READDIR, bad root_ref, should EINVAL");

    ltest_finalize (cache, krm);
    json_decref (root);
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
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char valref2_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref_test_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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
     * "symlinkNS" : symlink to "boz" in namespace=primary
     *
     * root_ref
     * "dirref" : dirref to dirref_ref
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    blobref_hash ("sha1", "efgh", 4, valref2_ref, sizeof (valref2_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref2_ref, "efgh", 4));

    dirref_test = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref_test, "dummy", "dummy", 5);

    treeobj_hash ("sha1", dirref_test, dirref_test_ref, sizeof (dirref_test_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_test_ref, dirref_test));

    dir = treeobj_create_dir ();
    _treeobj_insert_entry_val (dir, "val", "bar", 3);

    dirref = treeobj_create_dir ();
    _treeobj_insert_entry_valref (dirref, "valref", valref_ref);
    _treeobj_insert_entry_valref (dirref, "valref_with_dirref", dirref_test_ref);
    _treeobj_insert_entry_val (dirref, "val", "foo", 3);
    treeobj_insert_entry (dirref, "dir", dir);
    _treeobj_insert_entry_symlink (dirref, "symlink", NULL, "baz");
    _treeobj_insert_entry_symlink (dirref, "symlinkNS", KVS_PRIMARY_NAMESPACE, "boz");

    valref_multi = treeobj_create_valref (valref_ref);
    treeobj_append_blobref (valref_multi, valref2_ref);

    treeobj_insert_entry (dirref, "valref_multi", valref_multi);

    valref_multi_with_dirref = treeobj_create_valref (valref_ref);
    treeobj_append_blobref (valref_multi_with_dirref, dirref_test_ref);

    treeobj_insert_entry (dirref, "valref_multi_with_dirref", valref_multi_with_dirref);

    treeobj_hash ("sha1", dirref, dirref_ref, sizeof (dirref_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref", dirref_ref);

    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup dir via dirref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on path dirref");
    check_value (lh, dirref, "lookup dirref");

    /* lookup value via valref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             owner_cred,
                             0,
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
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref_with_dirref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dirref.valref_with_dirref");
    check_treeobj_val_result (lh, "lookup dirref.valref_with_dirref");

    /* Lookup value via valref with multiple blobrefs */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref_multi",
                             owner_cred,
                             0,
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
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref_multi_with_dirref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dirref.valref_multi_with_dirref");
    check_treeobj_val_result (lh, "lookup dirref.valref_multi_with_dirref");

    /* lookup value via val */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on path dirref.val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup dirref.val");
    json_decref (test);

    /* lookup dir via dir */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.dir",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on path dirref.dir");
    check_value (lh, dir, "lookup dirref.dir");

    /* lookup symlink */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.symlink",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlink");
    test = treeobj_create_symlink (NULL, "baz");
    check_value (lh, test, "lookup dirref.symlink");
    json_decref (test);

    /* lookup symlinkNS */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.symlinkNS",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlinkNS");
    test = treeobj_create_symlink (KVS_PRIMARY_NAMESPACE, "boz");
    check_value (lh, test, "lookup dirref.symlinkNS");
    json_decref (test);

    /* lookup dirref treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref (treeobj)");
    test = treeobj_create_dirref (dirref_ref);
    check_value (lh, test, "lookup dirref treeobj");
    json_decref (test);

    /* lookup valref treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref.valref (treeobj)");
    test = treeobj_create_valref (valref_ref);
    check_value (lh, test, "lookup dirref.valref treeobj");
    json_decref (test);

    /* lookup val treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.val",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref.val (treeobj)");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup dirref.val treeobj");
    json_decref (test);

    /* lookup dir treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.dir",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref.dir (treeobj)");
    check_value (lh, dir, "lookup dirref.dir treeobj");

    /* lookup symlink treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.symlink",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlink (treeobj)");
    test = treeobj_create_symlink (NULL, "baz");
    check_value (lh, test, "lookup dirref.symlink treeobj");
    json_decref (test);

    /* lookup symlinkNS treeobj */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.symlinkNS",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on path dirref.symlinkNS (treeobj)");
    test = treeobj_create_symlink (KVS_PRIMARY_NAMESPACE, "boz");
    check_value (lh, test, "lookup dirref.symlinkNS treeobj");
    json_decref (test);

    ltest_finalize (cache, krm);
    json_decref (dirref_test);
    json_decref (dir);
    json_decref (dirref);
    json_decref (valref_multi);
    json_decref (valref_multi_with_dirref);
    json_decref (root);
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
    char dirref_ref[BLOBREF_MAX_STRING_SIZE];
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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
     * "symlinkNS" : symlink to "symlinkNSstr" in namespace=primary
     * "symlinkNS1" : symlink to "symlinkNS2" in namespace=primary
     * "symlinkNS2" : symlink to "symlinkNS1" in namespace=primary
     * "ns2symlink" : symlink to "symlink2ns" in namespace=primary
     * "symlink2ns" : symlink to "ns2symlink"
     * "val" : val to "foo"
     * "valref" : valref to valref_ref
     * "dirref" : dirref to dirref_ref
     * "dir" : dir w/ "val" : val to "baz"
     * "dirref_bad" : dirref to valref_ref
     * "dirref_multi" : dirref to [ dirref_ref, dirref_ref ]
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    dirref = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref, "val", "bar", 3);
    treeobj_hash ("sha1", dirref, dirref_ref, sizeof (dirref_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    dir = treeobj_create_dir ();
    _treeobj_insert_entry_val (dir, "val", "baz", 3);

    root = treeobj_create_dir ();
    _treeobj_insert_entry_symlink (root, "symlink", NULL, "symlinkstr");
    _treeobj_insert_entry_symlink (root, "symlink1", NULL, "symlink2");
    _treeobj_insert_entry_symlink (root, "symlink2", NULL, "symlink1");
    _treeobj_insert_entry_symlink (root, "symlinkNS", KVS_PRIMARY_NAMESPACE, "symlinkNSstr");
    _treeobj_insert_entry_symlink (root, "symlinkNS1", KVS_PRIMARY_NAMESPACE, "symlinkNS2");
    _treeobj_insert_entry_symlink (root, "symlinkNS2", KVS_PRIMARY_NAMESPACE, "symlinkNS1");
    _treeobj_insert_entry_symlink (root, "symlinkNS2symlink", KVS_PRIMARY_NAMESPACE, "symlink2symlinkNS");
    _treeobj_insert_entry_symlink (root, "symlink2symlinkNS", NULL, "symlinkNS2symlink");
    _treeobj_insert_entry_val (root, "val", "foo", 3);
    _treeobj_insert_entry_valref (root, "valref", valref_ref);
    _treeobj_insert_entry_dirref (root, "dirref", dirref_ref);
    treeobj_insert_entry (root, "dir", dir);
    _treeobj_insert_entry_dirref (root, "dirref_bad", valref_ref);

    dirref_multi = treeobj_create_dirref (dirref_ref);
    treeobj_append_blobref (dirref_multi, dirref_ref);

    treeobj_insert_entry (root, "dirref_multi", dirref_multi);

    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* Lookup non-existent field.  Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "foo",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on bad path in path");
    check_value (lh, NULL, "lookup bad path");

    /* Lookup path w/ val in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val.foo",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on val in path");
    check_value (lh, NULL, "lookup val in path");

    /* Lookup path w/ valref in middle, Not ENOENT - caller of lookup
     * decides what to do with entry not found */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "valref.foo",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on valref in path");
    check_value (lh, NULL, "lookup valref in path");

    /* Lookup path w/ dir in middle, should get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dir.foo",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dir in path");
    check_error (lh, ENOTRECOVERABLE, "lookup dir in path");

    /* Lookup path w/ infinite link loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlink1",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on link loop");
    check_error (lh, ELOOP, "lookup infinite links");

    /* Lookup path w/ infinite symlinkNS loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkNS1",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkNS loop");
    check_error (lh, ELOOP, "lookup infinite symlinkNS loop");

    /* Lookup path w/ infinite symlink w/ & w/o namespace loop, should get ELOOP */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkNS2symlink",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlink (w/ & w/o namespace) loop");
    check_error (lh, ELOOP, "lookup infinite symlink (w/ & w/o namespace) loop");

    /* Lookup a dirref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on dirref");
    check_error (lh, EINVAL, "lookup dirref, expecting link");

    /* Lookup a dir, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dir",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on dir");
    check_error (lh, EINVAL, "lookup dir, expecting link");

    /* Lookup a valref, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "valref",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on valref");
    check_error (lh, EINVAL, "lookup valref, expecting link");

    /* Lookup a val, but expecting a link, should get EINVAL. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create on val");
    check_error (lh, EINVAL, "lookup val, expecting link");

    /* Lookup a dirref, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dirref");
    check_error (lh, EISDIR, "lookup dirref, not expecting dirref");

    /* Lookup a dir, but don't expect a dir, should get EISDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dir",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dir");
    check_error (lh, EISDIR, "lookup dir, not expecting dir");

    /* Lookup a valref, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "valref",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on valref");
    check_error (lh, ENOTDIR, "lookup valref, expecting dir");

    /* Lookup a val, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on val");
    check_error (lh, ENOTDIR, "lookup val, expecting dir");

    /* Lookup a symlink, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlink",
                             owner_cred,
                             FLUX_KVS_READLINK | FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on symlink");
    check_error (lh, ENOTDIR, "lookup symlink, expecting dir");

    /* Lookup a symlinkNS, but expecting a dir, should get ENOTDIR. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkNS",
                             owner_cred,
                             FLUX_KVS_READLINK | FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on symlinkNS");
    check_error (lh, ENOTDIR, "lookup symlinkNS, expecting dir");

    /* Lookup a dirref that doesn't point to a dir, should get ENOTRECOVERABLE. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref_bad",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on dirref_bad");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_bad");

    /* Lookup a dirref that doesn't point to a dir, in middle of path,
     * should get ENOTRECOVERABLE. */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref_bad.val",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on dirref_bad, in middle of path");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_bad, in middle of path");

    /* Lookup with an invalid root_ref, should get EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             valref_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on bad root_ref");
    check_error (lh, EINVAL, "lookup bad root_ref");

    /* Lookup dirref with multiple blobrefs, should get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref_multi",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on dirref_multi");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_multi");

    /* Lookup path w/ dirref w/ multiple blobrefs in middle, should
     * get ENOTRECOVERABLE */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref_multi.foo",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on dirref_multi, part of path");
    check_error (lh, ENOTRECOVERABLE, "lookup dirref_multi, part of path");

    /* This last test to just to make sure if we call lookup ()
     * multiple times, we can the same error each time.
     */

    /* Lookup with an invalid root_ref, should get EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             valref_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on bad root_ref for double call test");
    ok (lookup (lh) == LOOKUP_PROCESS_ERROR,
        "lookup returns LOOKUP_PROCESS_ERROR on first call");
    ok (lookup (lh) == LOOKUP_PROCESS_ERROR,
        "lookup still returns LOOKUP_PROCESS_ERROR on second call");
    lookup_destroy (lh);

    ltest_finalize (cache, krm);
    json_decref (dirref);
    json_decref (dir);
    json_decref (root);
    json_decref (dirref_multi);
}

void lookup_security (void) {
    json_t *root;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_ref[BLOBREF_MAX_STRING_SIZE];
    struct flux_msg_cred owner_5 = { .userid = 5, .rolemask = FLUX_ROLE_OWNER };
    struct flux_msg_cred user_6 = { .userid = 6, .rolemask = FLUX_ROLE_USER};
    struct flux_msg_cred owner_6 = { .userid = 6, .rolemask = FLUX_ROLE_OWNER};
    struct flux_msg_cred owner_7 = { .userid = 7, .rolemask = FLUX_ROLE_OWNER};
    struct flux_msg_cred user_7 = { .userid = 7, .rolemask = FLUX_ROLE_USER};

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root_ref
     * "val" : val to "foo"
     */

    root = treeobj_create_dir ();
    _treeobj_insert_entry_val (root, "val", "foo", 3);

    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 5);
    setup_kvsroot (krm, "altnamespace", cache, root_ref, 6);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_5,
                             0,
                             NULL)) != NULL,
        "lookup_create on val with rolemask owner and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val with rolemask owner and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_5,
                             0,
                             NULL)) != NULL,
        "lookup_create on val with rolemask user and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val with rolemask user and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             user_6,
                             0,
                             NULL)) != NULL,
        "lookup_create on val with rolemask user and invalid owner");
    check_error (lh, EPERM, "lookup_create on val with rolemask user and invalid owner");

    /* if root_ref is set, namespace checks won't occur */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             0,
                             "val",
                             user_6,
                             0,
                             NULL)) != NULL,
        "lookup_create on val with rolemask user and invalid owner w/ root_ref");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val with rolemask user and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "altnamespace",
                             NULL,
                             0,
                             "val",
                             owner_6,
                             0,
                             NULL)) != NULL,
        "lookup_create on val on namespace altnamespace with rolemask owner and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val on namespace altnamespace with rolemask owner and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "altnamespace",
                             NULL,
                             0,
                             "val",
                             owner_7,
                             0,
                             NULL)) != NULL,
        "lookup_create on val on namespace altnamespace with rolemask owner and invalid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val on namespace altnamespace with rolemask owner and invalid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "altnamespace",
                             NULL,
                             0,
                             "val",
                             user_6,
                             0,
                             NULL)) != NULL,
        "lookup_create on val on namespace altnamespace with rolemask user and valid owner");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "lookup val on namespace altnamespace with rolemask user and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "altnamespace",
                             NULL,
                             0,
                             "val",
                             user_7,
                             0,
                             NULL)) != NULL,
        "lookup_create on val on namespace altnamespace with rolemask user and invalid owner");
    check_error (lh, EPERM, "lookup_create on val on namespace altnamespace with rolemask user and invalid owner");

    ltest_finalize (cache, krm);
    json_decref (root);
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
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref3_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref2_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref1_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    dirref3 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref3, "val", "baz", 3);
    treeobj_hash ("sha1", dirref3, dirref3_ref, sizeof (dirref3_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref3_ref, dirref3));

    dir = treeobj_create_dir ();
    _treeobj_insert_entry_val (dir, "val", "bar", 3);

    dirref2 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref2, "val", "foo", 3);
    _treeobj_insert_entry_valref (dirref2, "valref", valref_ref);
    treeobj_insert_entry (dirref2, "dir", dir);
    _treeobj_insert_entry_dirref (dirref2, "dirref", dirref3_ref);
    _treeobj_insert_entry_symlink (dirref2, "symlink", NULL, "dirref2.val");
    treeobj_hash ("sha1", dirref2, dirref2_ref, sizeof (dirref2_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref2_ref, dirref2));

    dirref1 = treeobj_create_dir ();
    _treeobj_insert_entry_symlink (dirref1, "link2dirref", NULL, "dirref2");
    _treeobj_insert_entry_symlink (dirref1, "link2val", NULL, "dirref2.val");
    _treeobj_insert_entry_symlink (dirref1, "link2valref", NULL, "dirref2.valref");
    _treeobj_insert_entry_symlink (dirref1, "link2dir", NULL, "dirref2.dir");
    _treeobj_insert_entry_symlink (dirref1, "link2symlink", NULL, "dirref2.symlink");
    treeobj_hash ("sha1", dirref1, dirref1_ref, sizeof (dirref1_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref1_ref, dirref1));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref1", dirref1_ref);
    _treeobj_insert_entry_dirref (root, "dirref2", dirref2_ref);
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup val, follow two links */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.symlink",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create link to val via two links");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "val via two links");
    json_decref (test);

    /* lookup val, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create link to val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.link2dirref.val");
    json_decref (test);

    /* lookup valref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create link to valref");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.link2dirref.valref");
    json_decref (test);

    /* lookup dir, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.dir",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create link to dir");
    check_value (lh, dir, "dirref1.link2dirref.dir");

    /* lookup dirref, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.dirref",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create link to dirref");
    check_value (lh, dirref3, "dirref1.link2dirref.dirref");

    /* lookup symlink, link is middle of path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref.symlink",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create link to symlink");
    test = treeobj_create_symlink (NULL, "dirref2.val");
    check_value (lh, test, "dirref1.link2dirref.symlink");
    json_decref (test);

    /* lookup val, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create link to val (last part path)");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.link2val");
    json_decref (test);

    /* lookup valref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create link to valref (last part path)");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.link2valref");
    json_decref (test);

    /* lookup dir, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dir",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create link to dir (last part path)");
    check_value (lh, dir, "dirref1.link2dir");

    /* lookup dirref, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2dirref",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create link to dirref (last part path)");
    check_value (lh, dirref2, "dirref1.link2dirref");

    /* lookup symlink, link is last part in path */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.link2symlink",
                             owner_cred,
                             FLUX_KVS_READLINK,
                             NULL)) != NULL,
        "lookup_create link to symlink (last part path)");
    test = treeobj_create_symlink (NULL, "dirref2.symlink");
    check_value (lh, test, "dirref1.link2symlink");
    json_decref (test);

    ltest_finalize (cache, krm);
    json_decref (dirref3);
    json_decref (dir);
    json_decref (dirref2);
    json_decref (dirref1);
    json_decref (root);
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
    char dirref1_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref2_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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
    _treeobj_insert_entry_val (dirref1, "val", "foo", 3);
    treeobj_hash ("sha1", dirref1, dirref1_ref, sizeof (dirref1_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref1_ref, dirref1));

    dirref2 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref2, "val", "bar", 3);
    treeobj_hash ("sha1", dirref2, dirref2_ref, sizeof (dirref2_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref2_ref, dirref2));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref1", dirref1_ref);
    _treeobj_insert_entry_dirref (root, "dirref2", dirref2_ref);
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup val, alt root-ref dirref1_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             dirref1_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create val w/ dirref1 root_ref");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    /* lookup val, alt root-ref dirref2_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             dirref2_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create val w/ dirref2 root_ref");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    /* lookup val, alt root-ref dirref1_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             dirref1_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create val w/ dirref1 root_ref & input namespace");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    /* lookup val, alt root-ref dirref2_ref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             dirref2_ref,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create val w/ dirref2 root_ref & input namespace");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "alt root val");
    json_decref (test);

    ltest_finalize (cache, krm);
    json_decref (dirref1);
    json_decref (dirref2);
    json_decref (root);
}

/* lookup tests on root dir, if in a symlink */
void lookup_root_symlink (void) {
    json_t *root;
    json_t *dirref;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_ref[BLOBREF_MAX_STRING_SIZE];
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * valref_ref
     * "abcd"
     *
     * dirref_ref
     * "symlinkroot" : symlink to "."
     *
     * root_ref
     * "val" : val to "foo"
     * "symlinkroot" : symlink to "."
     * "dirref" : dirref to dirref_ref
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));
    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    dirref = treeobj_create_dir ();
    _treeobj_insert_entry_symlink (dirref, "symlinkroot", NULL, ".");
    treeobj_hash ("sha1", dirref, dirref_ref, sizeof (dirref_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_val (root, "val", "foo", 3);
    _treeobj_insert_entry_symlink (root, "symlinkroot", NULL, ".");
    _treeobj_insert_entry_dirref (root, "dirref", dirref_ref);
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* flags = 0, should error EISDIR */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkroot",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkroot, no flags, works");
    check_error (lh, EISDIR, "symlinkroot no flags");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkroot",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on symlinkroot w/ flag = FLUX_KVS_READDIR, works");
    check_value (lh, root, "symlinkroot w/ FLUX_KVS_READDIR");

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.symlinkroot",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on dirref.symlinkroot w/ flag = FLUX_KVS_READDIR, works");
    check_value (lh, root, "dirref.symlinkroot w/ FLUX_KVS_READDIR");

    /* tricky, this returns a symlink now, not the root dir */
    /* flags = FLUX_KVS_TREEOBJ, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkroot",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create on symlinkroot w/ flag = FLUX_KVS_TREEOBJ, works");
    test = treeobj_create_symlink (NULL, ".");
    check_value (lh, test, "symlinkroot w/ FLUX_KVS_TREEOBJ");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlinkroot.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkroot.val, works");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "symlinkroot.val");
    json_decref (test);

    /* flags = FLUX_KVS_READDIR, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             dirref_ref,
                             0,
                             "symlinkroot",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on symlinkroot w/ flag = FLUX_KVS_READDIR, and alt root_ref, works");
    check_value (lh, dirref, "symlinkroot w/ FLUX_KVS_READDIR, and alt root_ref");

    /* flags = FLUX_KVS_READDIR, bad root_ref, should error EINVAL */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             valref_ref,
                             0,
                             "symlinkroot",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create on symlinkroot w/ flag = FLUX_KVS_READDIR, bad root_ref, should EINVAL");
    check_error (lh, EINVAL, "symlinkroot w/ FLUX_KVS_READDIR, bad root_ref, should EINVAL");

    ltest_finalize (cache, krm);
    json_decref (dirref);
    json_decref (root);
}

/* lookup symlinkNS tests */
void lookup_symlinkNS (void) {
    json_t *rootA;
    json_t *rootB;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_refA[BLOBREF_MAX_STRING_SIZE];
    char root_refB[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root-refA
     * "val" : val to "1"
     * "symlinkNS2A-invalid" : symlinkNS to an invalid key in namespace=A
     * "symlinkNS2B-invalid" : symlinkNS to an invalid key in namespace=B
     * "symlinkNS2A" : symlinkNS to "." in namespace=A
     * "symlinkNS2B" : symlinkNS to "." in namespace=B
     * "symlinkNS2A-val" : symlinkNS to "val" in namespace=A
     * "symlinkNS2B-val" : symlinkNS to "val" in namespace=B
     *
     * root-refB
     * "val" : val to "2"
     */

    rootA = treeobj_create_dir ();
    _treeobj_insert_entry_val (rootA, "val", "1", 1);
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2A-invalid", "A", "foobar");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2B-invalid", "A", "foobar");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2A", "A", ".");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2B", "B", ".");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2A-val", "A", "val");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2B-val", "B", "val");
    treeobj_hash ("sha1", rootA, root_refA, sizeof (root_refA));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_refA, rootA));

    rootB = treeobj_create_dir ();
    _treeobj_insert_entry_val (rootB, "val", "2", 1);
    treeobj_hash ("sha1", rootB, root_refB, sizeof (root_refB));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_refB, rootB));

    setup_kvsroot (krm, "A", cache, root_refA, 0);
    setup_kvsroot (krm, "B", cache, root_refB, 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2A-invalid",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2A-invalid on namespace A");
    check_value (lh, NULL, "symlinkNS2A-invalid on namespace A");

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B-invalid",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2B-invalid on namespace A");
    check_value (lh, NULL, "symlinkNS2B-invalid on namespace A");

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2A.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2A.val on namespace A");
    test = treeobj_create_val ("1", 1);
    check_value (lh, test, "symlinkNS2A.val on namespace A");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2B.val on namespace A");
    test = treeobj_create_val ("2", 1);
    check_value (lh, test, "symlinkNS2B.val on namespace A");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2A-val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2A-val on namespace A");
    test = treeobj_create_val ("1", 1);
    check_value (lh, test, "symlinkNS2A-val on namespace A");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B-val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlinkNS2B-val on namespace A");
    test = treeobj_create_val ("2", 1);
    check_value (lh, test, "symlinkNS2B-val on namespace A");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2A",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create symlinkNS2A on namespace A, readdir");
    check_value (lh, rootA, "symlinkNS2A on namespace A, readdir");

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create symlinkNS2B on namespace A, readdir");
    check_value (lh, rootB, "symlinkNS2B on namespace A, readdir");

    ltest_finalize (cache, krm);
    json_decref (rootA);
    json_decref (rootB);
}

void lookup_symlinkNS_security (void) {
    json_t *rootA;
    json_t *rootB;
    json_t *rootC;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_refA[BLOBREF_MAX_STRING_SIZE];
    char root_refB[BLOBREF_MAX_STRING_SIZE];
    char root_refC[BLOBREF_MAX_STRING_SIZE];
    struct flux_msg_cred owner_1000 = { .rolemask = FLUX_ROLE_OWNER,
                                        .userid = 1000 };
    struct flux_msg_cred user_1000 = { .rolemask = FLUX_ROLE_USER,
                                       .userid = 1000 };

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root-refA
     * "val" : val to "1"
     * "symlinkNS2B" : symlinkNS to "." in namespace=B
     * "symlinkNS2C" : symlinkNS to "." in namespace=C
     *
     * root-refB
     * "val" : val to "2"
     *
     * root-refC
     * "val" : val to "3"
     */

    rootA = treeobj_create_dir ();
    _treeobj_insert_entry_val (rootA, "val", "1", 1);
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2B", "B", ".");
    _treeobj_insert_entry_symlink (rootA, "symlinkNS2C", "C", ".");
    treeobj_hash ("sha1", rootA, root_refA, sizeof (root_refA));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_refA, rootA));

    rootB = treeobj_create_dir ();
    _treeobj_insert_entry_val (rootB, "val", "2", 1);
    treeobj_hash ("sha1", rootB, root_refB, sizeof (root_refB));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_refB, rootB));

    rootC = treeobj_create_dir ();
    _treeobj_insert_entry_val (rootC, "val", "3", 1);
    treeobj_hash ("sha1", rootC, root_refC, sizeof (root_refC));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_refC, rootC));

    setup_kvsroot (krm, "A", cache, root_refA, 1000);
    setup_kvsroot (krm, "B", cache, root_refB, 1000);
    setup_kvsroot (krm, "C", cache, root_refC, 2000);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B.val",
                             owner_1000,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkNS2B.val with rolemask owner");
    test = treeobj_create_val ("2", 1);
    check_value (lh, test, "lookup symlinkNS2B.val with rolemask owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2C.val",
                             owner_1000,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkNS2C.val with rolemask owner");
    test = treeobj_create_val ("3", 1);
    check_value (lh, test, "lookup symlinkNS2C.val with rolemask owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2B.val",
                             user_1000,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkNS2B.val with rolemask user and valid owner");
    test = treeobj_create_val ("2", 1);
    check_value (lh, test, "lookup symlinkNS2B.val with rolemask user and valid owner");
    json_decref (test);

    ok ((lh = lookup_create (cache,
                             krm,
                             "A",
                             NULL,
                             0,
                             "symlinkNS2C.val",
                             user_1000,
                             0,
                             NULL)) != NULL,
        "lookup_create on symlinkNS2C.val with rolemask user and invalid owner");
    check_error (lh, EPERM, "lookup_create on symlinkNS2C.val with rolemask user and invalid owner");

    ltest_finalize (cache, krm);
    json_decref (rootA);
    json_decref (rootB);
    json_decref (rootC);
}

/* lookup stall namespace tests */
void lookup_stall_namespace (void) {
    json_t *root1;
    json_t *root2;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_ref1[BLOBREF_MAX_STRING_SIZE];
    char root_ref2[BLOBREF_MAX_STRING_SIZE];
    const char *tmp;

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root-ref1
     * "val" : val to "foo"
     *
     * root-ref2
     * "val" : val to "bar"
     */

    root1 = treeobj_create_dir ();
    _treeobj_insert_entry_val (root1, "val", "foo", 3);
    treeobj_hash ("sha1", root1, root_ref1, sizeof (root_ref1));

    root2 = treeobj_create_dir ();
    _treeobj_insert_entry_val (root2, "val", "bar", 3);
    treeobj_hash ("sha1", root2, root_ref2, sizeof (root_ref2));

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref1, root1));
    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref2, root2));

    /* do not insert root into kvsroot_mgr until later for these stall tests */

    /* First test for stall on normal namespace input */

    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest");
    ok (lookup (lh) == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE,
        "lookup stalled on missing namespace");
    ok ((tmp = lookup_missing_namespace (lh)) != NULL,
        "lookup_missing_namespace returned non-NULL");
    ok (streq (tmp, KVS_PRIMARY_NAMESPACE),
        "lookup_missing_namespace returned correct namespace");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref1, 0);

    /* lookup "val", should succeed now */
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "val");
    json_decref (test);

    /* lookup "val" should succeed cleanly */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest #2");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "val #2");
    json_decref (test);

    /* Second test for stall on namespace input */

    ok ((lh = lookup_create (cache,
                             krm,
                             "foo",
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest");
    ok (lookup (lh) == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE,
        "lookup stalled on missing namespace");
    ok ((tmp = lookup_missing_namespace (lh)) != NULL,
        "lookup_missing_namespace returned non-NULL");
    ok (streq (tmp, "foo"),
        "lookup_missing_namespace returned correct namespace");

    setup_kvsroot (krm, "foo", cache, root_ref2, 0);

    /* lookup "val", should succeed now */
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "val");
    json_decref (test);

    /* lookup val on namespace foo should succeed cleanly */
    ok ((lh = lookup_create (cache,
                             krm,
                             "foo",
                             NULL,
                             0,
                             "val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest #2");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "val #2");
    json_decref (test);

    /* Third, stall on root of kvs */

    ok ((lh = lookup_create (cache,
                             krm,
                             "roottest",
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_TREEOBJ,
                             NULL)) != NULL,
        "lookup_create stalltest on root .");
    ok (lookup (lh) == LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE,
        "lookup stalled on missing namespace");
    ok ((tmp = lookup_missing_namespace (lh)) != NULL,
        "lookup_missing_namespace returned non-NULL");
    ok (streq (tmp, "roottest"),
        "lookup_missing_namespace returned correct namespace");

    setup_kvsroot (krm, "roottest", cache, root_ref1, 0);

    /* lookup ".", should succeed now */
    test = treeobj_create_dirref (root_ref1);
    check_value (lh, test, ".");
    json_decref (test);

    ltest_finalize (cache, krm);
    json_decref (root1);
    json_decref (root2);
}

/* lookup stall ref tests on root */
void lookup_stall_ref_root (void) {
    json_t *root;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * root-ref
     * "val" : val to "foo"
     */

    root = treeobj_create_dir ();
    _treeobj_insert_entry_val (root, "val", "foo", 3);
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup root ".", should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create stalltest \".\"");
    check_stall (lh, EAGAIN, 1, root_ref, "root \".\" stall");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    /* lookup root ".", should succeed */
    check_value (lh, root, "root \".\" #1");

    /* lookup root ".", now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             ".",
                             owner_cred,
                             FLUX_KVS_READDIR,
                             NULL)) != NULL,
        "lookup_create stalltest \".\"");
    check_value (lh, root, "root \".\" #2");

    ltest_finalize (cache, krm);
    json_decref (root);
}

/* lookup stall ref tests */
void lookup_stall_ref (void) {
    json_t *root;
    json_t *valref_tmp1;
    json_t *valref_tmp2;
    json_t *valref_tmp3;
    json_t *dirref1;
    json_t *dirref2;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char valref1_ref[BLOBREF_MAX_STRING_SIZE];
    char valref2_ref[BLOBREF_MAX_STRING_SIZE];
    char valref3_ref[BLOBREF_MAX_STRING_SIZE];
    char valref4_ref[BLOBREF_MAX_STRING_SIZE];
    char valrefmisc1_ref[BLOBREF_MAX_STRING_SIZE];
    char valrefmisc2_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref1_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref2_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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
     * "valref" : valref to valref1_ref
     * "valref_multi" : valref to [ valref1_ref, valref2_ref ]
     * "valref_multi2" : valref to [ valref3_ref, valref4_ref ]
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

    blobref_hash ("sha1", "abcd", 4, valref1_ref, sizeof (valref1_ref));
    blobref_hash ("sha1", "efgh", 4, valref2_ref, sizeof (valref2_ref));
    blobref_hash ("sha1", "ijkl", 4, valref3_ref, sizeof (valref3_ref));
    blobref_hash ("sha1", "mnop", 4, valref4_ref, sizeof (valref4_ref));
    blobref_hash ("sha1", "foobar", 4, valrefmisc1_ref, sizeof (valrefmisc1_ref));
    blobref_hash ("sha1", "foobaz", 4, valrefmisc2_ref, sizeof (valrefmisc2_ref));

    dirref1 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref1, "val", "foo", 3);
    _treeobj_insert_entry_valref (dirref1, "valref", valref1_ref);
    valref_tmp1 = treeobj_create_valref (valref1_ref);
    treeobj_append_blobref (valref_tmp1, valref2_ref);
    treeobj_insert_entry (dirref1, "valref_multi", valref_tmp1);
    valref_tmp2 = treeobj_create_valref (valref3_ref);
    treeobj_append_blobref (valref_tmp2, valref4_ref);
    treeobj_insert_entry (dirref1, "valref_multi2", valref_tmp2);
    _treeobj_insert_entry_valref (dirref1, "valrefmisc", valrefmisc1_ref);
    valref_tmp3 = treeobj_create_valref (valrefmisc1_ref);
    treeobj_append_blobref (valref_tmp3, valrefmisc2_ref);
    treeobj_insert_entry (dirref1, "valrefmisc_multi", valref_tmp3);

    treeobj_hash ("sha1", dirref1, dirref1_ref, sizeof (dirref1_ref));

    dirref2 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref2, "val", "bar", 3);
    treeobj_hash ("sha1", dirref2, dirref2_ref, sizeof (dirref2_ref));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref1", dirref1_ref);
    _treeobj_insert_entry_dirref (root, "dirref2", dirref2_ref);
    _treeobj_insert_entry_symlink (root, "symlink", NULL, "dirref2");
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* do not insert entries into cache until later for these stall tests */

    /* lookup dirref1.val, should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.val");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref1.val stall #1");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    /* next call to lookup, should stall */
    check_stall (lh, EAGAIN, 1, dirref1_ref, "dirref1.val stall #2");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref1_ref, dirref1));

    /* final call to lookup, should succeed */
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.val #1");
    json_decref (test);

    /* lookup dirref1.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create dirref1.val");
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.val #2");
    json_decref (test);

    /* lookup symlink.val, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             0,
                             "symlink.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest symlink.val");
    check_stall (lh, EAGAIN, 1, dirref2_ref, "symlink.val stall");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref2_ref, dirref2));

    /* lookup symlink.val, should succeed */
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "symlink.val #1");
    json_decref (test);

    /* lookup symlink.val, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "symlink.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create symlink.val");
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "symlink.val #2");
    json_decref (test);

    /* lookup dirref1.valref, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    check_stall (lh, EAGAIN, 1, valref1_ref, "dirref1.valref stall");

    (void)cache_insert (cache, create_cache_entry_raw (valref1_ref, "abcd", 4));

    /* lookup dirref1.valref, should succeed */
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.valref #1");
    json_decref (test);

    /* lookup dirref1.valref, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.valref #2");
    json_decref (test);

    /* lookup dirref1.valref_multi, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref_multi",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref_multi");
    /* should only be one missing ref, as we loaded one of the refs in
     * the 'valref' above */
    check_stall (lh, EAGAIN, 1, valref2_ref, "dirref1.valref_multi stall");

    (void)cache_insert (cache, create_cache_entry_raw (valref2_ref, "efgh", 4));

    /* lookup dirref1.valref_multi, should succeed */
    test = treeobj_create_val ("abcdefgh", 8);
    check_value (lh, test, "dirref1.valref_multi #1");
    json_decref (test);

    /* lookup dirref1.valref_multi, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref_multi",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("abcdefgh", 8);
    check_value (lh, test, "dirref1.valref_multi #2");
    json_decref (test);

    /* lookup dirref1.valref_multi2, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref_multi2",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref_multi2");
    /* should two missing refs, as we have not loaded either here */
    check_stall (lh, EAGAIN, 2, NULL, "dirref1.valref_multi2 stall");

    (void)cache_insert (cache, create_cache_entry_raw (valref3_ref, "ijkl", 4));
    (void)cache_insert (cache, create_cache_entry_raw (valref4_ref, "mnop", 4));

    /* lookup dirref1.valref_multi2, should succeed */
    test = treeobj_create_val ("ijklmnop", 8);
    check_value (lh, test, "dirref1.valref_multi2 #1");
    json_decref (test);

    /* lookup dirref1.valref_multi2, now fully cached, should succeed */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref_multi2",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    test = treeobj_create_val ("ijklmnop", 8);
    check_value (lh, test, "dirref1.valref_multi2 #2");
    json_decref (test);

    /* lookup dirref1.valrefmisc, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valrefmisc",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valrefmisc");
    /* don't call check_stall, this is primarily to test if callback
     * functions returning errors are caught */
    ok (lookup (lh) == LOOKUP_PROCESS_LOAD_MISSING_REFS,
        "dirref1.valrefmisc: lookup stalled");
    errno = 0;
    ok (lookup_iter_missing_refs (lh, lookup_ref_error, NULL) < 0
        && errno == EMLINK,
        "dirref1.valrefmisc: error & errno properly returned from callback error");
    lookup_destroy (lh);

    /* lookup dirref1.valrefmisc_multi, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valrefmisc_multi",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valrefmisc_multi");
    /* don't call check_stall, this is primarily to test if callback
     * functions returning errors are caught */
    ok (lookup (lh) == LOOKUP_PROCESS_LOAD_MISSING_REFS,
        "dirref1.valrefmisc_multi: lookup stalled");
    errno = 0;
    ok (lookup_iter_missing_refs (lh, lookup_ref_error, NULL) < 0
        && errno == EMLINK,
        "dirref1.valrefmisc_multi: error & errno properly returned from callback error");
    lookup_destroy (lh);

    ltest_finalize (cache, krm);
    json_decref (dirref1);
    json_decref (valref_tmp1);
    json_decref (valref_tmp2);
    json_decref (valref_tmp3);
    json_decref (dirref2);
    json_decref (root);
}

void lookup_stall_namespace_removed (void) {
    json_t *root;
    json_t *valref;
    json_t *dirref;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

    /* This cache is
     *
     * valref_ref
     * "abcd"

     * dirref_ref
     * "valref" : valref to valref_ref
     *
     * root_ref
     * "dirref" : dirref to dirref_ref
     *
     */

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));

    dirref = treeobj_create_dir ();
    valref = treeobj_create_valref (valref_ref);
    treeobj_insert_entry (dirref, "valref", valref);

    treeobj_hash ("sha1", dirref, dirref_ref, sizeof (dirref_ref));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref", dirref_ref);
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* do not insert entries into cache until later for these stall tests */

    /*
     * Check for each stall situation and that if namespace is
     * removed, that is caught
     */

    /* lookup dirref.valref, should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref.valref stall #1");

    /* insert cache entry, but remove namespace */

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    /* lookup should error out because namespace is now gone */

    check_error (lh, ENOTSUP, "namespace removed on root ref results in ENOTSUP");

    /* reset test */
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup dirref.valref, should stall on dirref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, dirref_ref, "dirref.valref stall #2");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    /* lookup should error out because namespace is now gone */

    check_error (lh, ENOTSUP, "namespace removed on dirref results in ENOTSUP");

    /* reset test */
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup dirref.valref, should stall on valref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, valref_ref, "dirref.valref stall #3");

    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    /* lookup should error out because namespace is now gone */

    check_error (lh, ENOTSUP, "namespace removed on valref results in ENOTSUP");

    /* reset test */
    cache_remove_entry (cache, root_ref);
    cache_remove_entry (cache, dirref_ref);
    cache_remove_entry (cache, valref_ref);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /*
     * Check for each stall situation and that if namespace is
     * replaced, it is caught
     */

    /* lookup dirref.valref, should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             user_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref.valref stall #1");

    /* insert cache entry, but remove namespace */

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 2);

    /* lookup should EPERM b/c owner of new namespace is different */

    check_error (lh, EPERM, "namespace replaced on root ref results in EPERM");

    /* reset test */
    kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup dirref.valref, should stall on dirref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             user_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, dirref_ref, "dirref.valref stall #2");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 2);

    /* lookup should EPERM b/c owner of new namespace is different */

    check_error (lh, EPERM, "namespace replaced on dirref results in EPERM");

    /* reset test */
    kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);


    /* lookup dirref.valref, should stall on valref */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref.valref",
                             user_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref");
    check_stall (lh, EAGAIN, 1, valref_ref, "dirref.valref stall #3");

    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 2);

    /* lookup should EPERM b/c owner of new namespace is different */

    check_error (lh, EPERM, "namespace replaced on valref results in EPERM");

    /* reset test */
    kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE);
    cache_remove_entry (cache, root_ref);
    cache_remove_entry (cache, dirref_ref);
    cache_remove_entry (cache, valref_ref);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /*
     * Now, similar tests to above, but we pass in a root_ref.  So the
     * checks on namespaces before is now no longer necessary and we
     * should eventually read the end value.
     */

    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             0,
                             "dirref.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref w/ root_ref");

    /* Now remove namespace */

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    /* Check for stalls, insert refs until should all succeed */

    check_stall (lh, EAGAIN, 1, root_ref, "dirref.valref stall #1 w/ root_ref");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    check_stall (lh, EAGAIN, 1, dirref_ref, "dirref.valref stall #2 w/ root_ref");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    check_stall (lh, EAGAIN, 1, valref_ref, "dirref.valref stall #3 w/ root_ref");

    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "lookup_create dirref.valref w/ root_ref");
    json_decref (test);

    /* reset test */
    cache_remove_entry (cache, root_ref);
    cache_remove_entry (cache, dirref_ref);
    cache_remove_entry (cache, valref_ref);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             0,
                             "dirref.valref",
                             user_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref.valref w/ root_ref & role user ");

    /* Now remove namespace and re-insert with different owner */

    ok (!kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE),
        "kvsroot_mgr_remove_root removed root successfully");

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 2);

    /* Check for stalls, insert refs until should all succeed */

    check_stall (lh, EAGAIN, 1, root_ref, "dirref.valref stall #1 w/ root_ref & role user");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    check_stall (lh, EAGAIN, 1, dirref_ref, "dirref.valref stall #2 w/ root_ref & role user");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref_ref, dirref));

    check_stall (lh, EAGAIN, 1, valref_ref, "dirref.valref stall #3 w/ root_ref & role user");

    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "lookup_create dirref.valref w/ root_ref & role user");
    json_decref (test);

    /* reset test */
    kvsroot_mgr_remove_root (krm, KVS_PRIMARY_NAMESPACE);
    cache_remove_entry (cache, root_ref);
    cache_remove_entry (cache, dirref_ref);
    cache_remove_entry (cache, valref_ref);
    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    ltest_finalize (cache, krm);
    json_decref (dirref);
    json_decref (valref);
    json_decref (root);
}

/* lookup stall ref tests - expire important cache entries after stalls */
void lookup_stall_ref_expire_cache_entries (void) {
    json_t *root;
    json_t *dirref1;
    json_t *dirref2;
    json_t *test;
    struct cache *cache;
    kvsroot_mgr_t *krm;
    lookup_t *lh;
    char valref_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref1_ref[BLOBREF_MAX_STRING_SIZE];
    char dirref2_ref[BLOBREF_MAX_STRING_SIZE];
    char root_ref[BLOBREF_MAX_STRING_SIZE];

    ltest_init (&cache, &krm);

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

    blobref_hash ("sha1", "abcd", 4, valref_ref, sizeof (valref_ref));

    dirref1 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref1, "val", "foo", 3);
    _treeobj_insert_entry_valref (dirref1, "valref", valref_ref);

    treeobj_hash ("sha1", dirref1, dirref1_ref, sizeof (dirref1_ref));

    dirref2 = treeobj_create_dir ();
    _treeobj_insert_entry_val (dirref2, "val", "bar", 3);
    treeobj_hash ("sha1", dirref2, dirref2_ref, sizeof (dirref2_ref));

    root = treeobj_create_dir ();
    _treeobj_insert_entry_dirref (root, "dirref1", dirref1_ref);
    _treeobj_insert_entry_dirref (root, "dirref2", dirref2_ref);
    _treeobj_insert_entry_symlink (root, "symlink", NULL, "dirref2");
    treeobj_hash ("sha1", root, root_ref, sizeof (root_ref));

    setup_kvsroot (krm, KVS_PRIMARY_NAMESPACE, cache, root_ref, 0);

    /* lookup dirref1.val, should stall on root */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.val");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref1.val stall #1");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    /* next call to lookup, should stall */
    check_stall (lh, EAGAIN, 1, dirref1_ref, "dirref1.val stall #2");

    ok (cache_count_entries (cache) == 1,
        "cache_count_entries returns 1");

    ok (cache_expire_entries (cache, 0) == 0,
        "cache_expire_entries expires 0 entries, b/c references appropriately taken");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref1_ref, dirref1));

    /* final call to lookup, should succeed */
    test = treeobj_create_val ("foo", 3);
    check_value (lh, test, "dirref1.val");
    json_decref (test);

    /* clear cache */

    ok (cache_expire_entries (cache, 0) == 2,
        "cache_expire_entries expires 2 entries");

    ok (cache_count_entries (cache) == 0,
        "cache_count_entries returns 0");

    ok ((lh = lookup_create (cache,
                             krm,
                             NULL,
                             root_ref,
                             0,
                             "symlink.val",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest symlink.val");
    check_stall (lh, EAGAIN, 1, root_ref, "symlink.val stall #1");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    check_stall (lh, EAGAIN, 1, dirref2_ref, "symlink.val stall #2");

    ok (cache_count_entries (cache) == 1,
        "cache_count_entries returns 1");

    ok (cache_expire_entries (cache, 0) == 0,
        "cache_expire_entries expires 0 entries, b/c references appropriately taken");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref2_ref, dirref2));

    /* lookup symlink.val, should succeed */
    test = treeobj_create_val ("bar", 3);
    check_value (lh, test, "symlink.val");
    json_decref (test);

    /* clear cache */

    ok (cache_expire_entries (cache, 0) == 2,
        "cache_expire_entries expires 2 entries");

    ok (cache_count_entries (cache) == 0,
        "cache_count_entries returns 0");

    /* lookup dirref1.valref, should stall */
    ok ((lh = lookup_create (cache,
                             krm,
                             KVS_PRIMARY_NAMESPACE,
                             NULL,
                             0,
                             "dirref1.valref",
                             owner_cred,
                             0,
                             NULL)) != NULL,
        "lookup_create stalltest dirref1.valref");
    check_stall (lh, EAGAIN, 1, root_ref, "dirref1.valref stall #1");

    (void)cache_insert (cache, create_cache_entry_treeobj (root_ref, root));

    check_stall (lh, EAGAIN, 1, dirref1_ref, "dirref1.valref stall #2");

    (void)cache_insert (cache, create_cache_entry_treeobj (dirref1_ref, dirref1));

    check_stall (lh, EAGAIN, 1, valref_ref, "dirref1.valref stall #3");

    ok (cache_count_entries (cache) == 2,
        "cache_count_entries returns 2");

    ok (cache_expire_entries (cache, 0) == 1,
        "cache_expire_entries expires 1 entry, only 1 entry has reference on it");

    (void)cache_insert (cache, create_cache_entry_raw (valref_ref, "abcd", 4));

    /* lookup dirref1.valref, should succeed */
    test = treeobj_create_val ("abcd", 4);
    check_value (lh, test, "dirref1.valref");
    json_decref (test);

    /* clear cache */

    ok (cache_expire_entries (cache, 0) == 2,
        "cache_expire_entries expires 2 entries");

    ok (cache_count_entries (cache) == 0,
        "cache_count_entries returns 0");

    ltest_finalize (cache, krm);
    json_decref (dirref1);
    json_decref (dirref2);
    json_decref (root);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api ();
    basic_api_errors ();
    basic_lookup ();

    lookup_root ();
    lookup_basic ();
    lookup_errors ();
    lookup_security ();
    lookup_links ();
    lookup_alt_root ();
    lookup_root_symlink ();
    lookup_symlinkNS ();
    lookup_symlinkNS_security ();
    lookup_stall_namespace ();
    lookup_stall_ref_root ();
    lookup_stall_ref ();
    lookup_stall_namespace_removed ();
    lookup_stall_ref_expire_cache_entries ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
