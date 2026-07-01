/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <errno.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "treeobj.h"
#include "kvs_treewalk.h"

static const char *hashtype = "sha1";

/* In-memory content store shared by the test server: a JSON object mapping
 * blobref string -> base64-ish raw blob (stored as a json string of bytes).
 * We store the raw bytes via a json binary-safe container (json_stringn).
 */
struct blobstore {
    json_t *map;        // blobref => json_string (raw bytes, len via json)
};

/* Store raw data, return its blobref (caller must free). */
static char *store (struct blobstore *bs, const void *data, size_t len)
{
    char ref[BLOBREF_MAX_STRING_SIZE];
    json_t *val;

    if (blobref_hash (hashtype, data, len, ref, sizeof (ref)) < 0)
        BAIL_OUT ("blobref_hash failed");
    if (!(val = json_stringn (data, len)))
        BAIL_OUT ("json_stringn failed");
    if (json_object_set_new (bs->map, ref, val) < 0)
        BAIL_OUT ("json_object_set failed");
    return strdup (ref);
}

/* Encode a treeobj and store it, returning its blobref (caller frees). */
static char *store_treeobj (struct blobstore *bs, json_t *obj)
{
    char *s;
    char *ref;
    if (!(s = treeobj_encode (obj)))
        BAIL_OUT ("treeobj_encode failed");
    ref = store (bs, s, strlen (s));
    free (s);
    return ref;
}

/* Test server: answer content.load (raw hash request) from the blobstore.
 */
static void load_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct blobstore *bs = arg;
    const void *hash;
    size_t hash_len;
    char ref[BLOBREF_MAX_STRING_SIZE];
    json_t *val;
    const char *data;
    size_t len;

    if (flux_request_decode_raw (msg, NULL, &hash, &hash_len) < 0)
        goto error;
    if (blobref_hashtostr (hashtype, hash, hash_len, ref, sizeof (ref)) < 0)
        goto error;
    if (!(val = json_object_get (bs->map, ref))) {
        errno = ENOENT;
        goto error;
    }
    data = json_string_value (val);
    len = json_string_length (val);
    if (flux_respond_raw (h, msg, data, len) < 0)
        BAIL_OUT ("flux_respond_raw failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error failed");
}

static int server_cb (flux_t *h, void *arg)
{
    struct flux_msg_handler_spec spec[] = {
        { FLUX_MSGTYPE_REQUEST, "content.load", load_cb, 0 },
        FLUX_MSGHANDLER_TABLE_END,
    };
    flux_msg_handler_t **handlers = NULL;
    int rc;

    if (flux_msg_handler_addvec (h, spec, arg, &handlers) < 0)
        BAIL_OUT ("flux_msg_handler_addvec failed");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    flux_msg_handler_delvec (handlers);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* Walk result collection */

struct collector {
    flux_t *h;                  // handle, for callbacks that issue requests
    zlistx_t *visited;          // paths visited (visit cb), in order
    int values;
    int symlinks;
    int valrefs_done;
    int valref_total_bytes;
    int valref_blob_errors;
    int errors;
    int valref_requests;        // times valref_request override was called
    int descend_calls;          // times descend predicate was called
    const char *prune_path;     // descend returns false for this path (or NULL)
    enum kvs_treewalk_error last_error;
};

static void diag_visit (void *arg, const char *path, json_t *treeobj)
{
    struct collector *c = arg;
    zlistx_add_end (c->visited, strdup (path));
}

static void on_value (void *arg, const char *path, json_t *treeobj)
{
    struct collector *c = arg;
    c->values++;
}

static void on_symlink (void *arg, const char *path, json_t *treeobj)
{
    struct collector *c = arg;
    c->symlinks++;
}

static void on_valref_done (void *arg,
                            const char *path,
                            json_t *treeobj,
                            int count,
                            const struct kvs_treewalk_blob *blobs)
{
    struct collector *c = arg;
    c->valrefs_done++;
    for (int i = 0; i < count; i++) {
        if (blobs[i].errnum) {
            c->valref_blob_errors++;
            continue;
        }
        const void *data;
        size_t len;
        if (flux_response_decode_raw (blobs[i].msg, NULL, &data, &len) == 0)
            c->valref_total_bytes += len;
    }
}

static void on_error (void *arg,
                     const char *path,
                     enum kvs_treewalk_error error,
                     int errnum)
{
    struct collector *c = arg;
    c->errors++;
    c->last_error = error;
}

/* valref_request override that still fetches via content.load (the blobstore
 * has no validate service), just to exercise the override seam.
 */
static flux_future_t *on_valref_request (void *arg, const char *blobref)
{
    struct collector *c = arg;
    c->valref_requests++;
    return content_load_byblobref (c->h, blobref, 0);
}

/* valref_request override that always fails, simulating a request-send error
 * so the walk aborts with a fatal error.
 */
static flux_future_t *on_valref_request_fail (void *arg, const char *blobref)
{
    errno = ENOMEM;
    return NULL;
}

/* descend predicate: count calls and prune the configured path. */
static bool on_descend (void *arg, const char *path, const char *blobref)
{
    struct collector *c = arg;
    c->descend_calls++;
    if (c->prune_path && streq (path, c->prune_path))
        return false;
    return true;
}

static struct kvs_treewalk_ops test_ops = {
    .visit = diag_visit,
    .value = on_value,
    .symlink = on_symlink,
    .valref_done = on_valref_done,
    .error = on_error,
};

static void collector_init (struct collector *c, flux_t *h)
{
    memset (c, 0, sizeof (*c));
    c->h = h;
    if (!(c->visited = zlistx_new ()))
        BAIL_OUT ("zlistx_new failed");
    /* items are plain strdup()'d strings, freed explicitly in
     * collector_fini
     */
}

static bool visited_has (struct collector *c, const char *path)
{
    char *s;
    for (s = zlistx_first (c->visited); s; s = zlistx_next (c->visited)) {
        if (streq (s, path))
            return true;
    }
    return false;
}

static void collector_fini (struct collector *c)
{
    char *s;
    while ((s = zlistx_detach (c->visited, NULL)))
        free (s);
    zlistx_destroy (&c->visited);
}

/* ------------------------------------------------------------------------ */

/* Build a tree:
 *   root/
 *     val      = "hello"                (inline val)
 *     link     -> target                (symlink)
 *     big      = valref [b1, b2, b3]    (3 blobs, 12 bytes)
 *     sub/     = dirref -> { deep = "x" (inline val) }
 * Returns root blobref (caller frees).
 */
static char *build_tree (struct blobstore *bs, int window_blobs)
{
    json_t *root, *sub;
    json_t *valref, *dirref_sub;
    char *r1, *r2, *r3, *subref, *rootref;

    /* valref blobs */
    r1 = store (bs, "aaaa", 4);
    r2 = store (bs, "bbbb", 4);
    r3 = store (bs, "cccc", 4);
    if (!(valref = treeobj_create_valref (r1))
        || treeobj_append_blobref (valref, r2) < 0
        || treeobj_append_blobref (valref, r3) < 0)
        BAIL_OUT ("create valref failed");

    /* subdir with one inline val, stored as a dirref */
    if (!(sub = treeobj_create_dir ()))
        BAIL_OUT ("create dir failed");
    json_t *deepval = treeobj_create_val ("x", 1);
    if (!deepval || treeobj_insert_entry (sub, "deep", deepval) < 0)
        BAIL_OUT ("insert deep failed");
    json_decref (deepval);
    subref = store_treeobj (bs, sub);
    if (!(dirref_sub = treeobj_create_dirref (subref)))
        BAIL_OUT ("create dirref failed");

    /* root dir */
    if (!(root = treeobj_create_dir ()))
        BAIL_OUT ("create root failed");
    json_t *v = treeobj_create_val ("hello", 5);
    json_t *l = treeobj_create_symlink (NULL, "target");
    if (!v || !l
        || treeobj_insert_entry (root, "val", v) < 0
        || treeobj_insert_entry (root, "link", l) < 0
        || treeobj_insert_entry (root, "big", valref) < 0
        || treeobj_insert_entry (root, "sub", dirref_sub) < 0)
        BAIL_OUT ("insert root entries failed");
    json_decref (v);
    json_decref (l);
    json_decref (valref);
    json_decref (dirref_sub);
    json_decref (sub);

    rootref = store_treeobj (bs, root);
    json_decref (root);
    free (r1);
    free (r2);
    free (r3);
    free (subref);
    return rootref;
}

static void test_basic_walk (flux_t *h, struct blobstore *bs, int window)
{
    char *rootref = build_tree (bs, window);
    struct collector c;
    struct kvs_treewalk *tw;

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', window, 0, &test_ops, &c);
    ok (tw != NULL, "kvs_treewalk_create window=%d works", window);
    ok (kvs_treewalk_run (tw) == 0, "kvs_treewalk_run window=%d ok", window);

    ok (c.errors == 0, "no errors reported (got %d)", c.errors);
    ok (c.values == 2, "two val objects visited (val + sub/deep) (got %d)",
        c.values);
    ok (c.symlinks == 1, "one symlink visited (got %d)", c.symlinks);
    ok (c.valrefs_done == 1, "one valref completed (got %d)", c.valrefs_done);
    ok (c.valref_total_bytes == 12,
        "valref delivered 12 bytes (got %d)", c.valref_total_bytes);
    ok (c.valref_blob_errors == 0, "no valref blob errors");

    ok (visited_has (&c, "val"), "visited root child 'val' (bare name)");
    ok (visited_has (&c, "sub"), "visited 'sub' dirref");
    ok (visited_has (&c, "sub/deep"),
        "visited 'sub/deep' (joined with '/')");

    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

static void test_separator (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_tree (bs, 4);
    struct collector c;
    struct kvs_treewalk *tw;

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '.', 4, 0, &test_ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with '.' separator ok");
    ok (visited_has (&c, "sub.deep"),
        "deep path joined with '.' separator");
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

static void test_missing_dirref (flux_t *h, struct blobstore *bs)
{
    json_t *root;
    char *rootref;
    struct collector c;
    struct kvs_treewalk *tw;
    /* a dirref pointing at a blobref that is not in the store */
    const char *bogus = "sha1-1234567890123456789012345678901234567890";
    json_t *dr = treeobj_create_dirref (bogus);

    if (!(root = treeobj_create_dir ())
        || treeobj_insert_entry (root, "baddir", dr) < 0)
        BAIL_OUT ("create root failed");
    json_decref (dr);
    rootref = store_treeobj (bs, root);
    json_decref (root);

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &test_ops, &c);
    ok (kvs_treewalk_run (tw) == 0,
        "walk with dangling dirref completes (run returns 0)");
    ok (c.errors == 1, "one error reported (got %d)", c.errors);
    ok (c.last_error == KVS_TREEWALK_ERROR_LOAD,
        "error category is LOAD");
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

static void test_missing_valref_blob (flux_t *h, struct blobstore *bs)
{
    json_t *root, *valref;
    char *rootref, *good;
    struct collector c;
    struct kvs_treewalk *tw;
    const char *bogus = "sha1-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    good = store (bs, "good", 4);
    if (!(valref = treeobj_create_valref (good))
        || treeobj_append_blobref (valref, bogus) < 0)
        BAIL_OUT ("create valref failed");
    if (!(root = treeobj_create_dir ())
        || treeobj_insert_entry (root, "v", valref) < 0)
        BAIL_OUT ("create root failed");
    json_decref (valref);
    rootref = store_treeobj (bs, root);
    json_decref (root);

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &test_ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with bad valref blob completes");
    ok (c.valrefs_done == 1, "valref_done still called with partial result");
    ok (c.valref_blob_errors == 1,
        "one valref blob reported missing (got %d)", c.valref_blob_errors);
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
    free (good);
}

/* Build a tree whose subdirectory is an INLINE dir (not a dirref), to
 * exercise the treeobj_is_dir recursion in classify():
 *   root/
 *     inline/   = dir { leaf = "y" }   (inline dir, no separate blob)
 * Returns root blobref (caller frees).
 */
static char *build_inline_dir_tree (struct blobstore *bs)
{
    json_t *root, *sub, *leaf;
    char *rootref;

    if (!(sub = treeobj_create_dir ()))
        BAIL_OUT ("create dir failed");
    if (!(leaf = treeobj_create_val ("y", 1))
        || treeobj_insert_entry (sub, "leaf", leaf) < 0)
        BAIL_OUT ("insert leaf failed");
    json_decref (leaf);

    if (!(root = treeobj_create_dir ())
        || treeobj_insert_entry (root, "inline", sub) < 0)
        BAIL_OUT ("create root failed");
    json_decref (sub);

    rootref = store_treeobj (bs, root);
    json_decref (root);
    return rootref;
}

/* An inline (not dirref) subdirectory is walked without an extra content
 * load, and its children are visited with the joined path.
 */
static void test_inline_dir (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_inline_dir_tree (bs);
    struct collector c;
    struct kvs_treewalk *tw;

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &test_ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with inline subdir ok");
    ok (c.errors == 0, "inline subdir: no errors (got %d)", c.errors);
    ok (c.values == 1, "inline subdir: leaf value visited (got %d)",
        c.values);
    ok (visited_has (&c, "inline"), "visited inline dir entry");
    ok (visited_has (&c, "inline/leaf"),
        "visited inline/leaf (recursed into inline dir)");
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

/* A valref_request override routes valref blob fetches through the caller;
 * the walk otherwise behaves identically.
 */
static void test_valref_request_override (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_tree (bs, 4);
    struct collector c;
    struct kvs_treewalk *tw;
    struct kvs_treewalk_ops ops = test_ops;
    ops.valref_request = on_valref_request;

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with valref_request override ok");
    ok (c.valref_requests == 3,
        "override issued for each of 3 valref blobs (got %d)",
        c.valref_requests);
    ok (c.valrefs_done == 1 && c.valref_total_bytes == 12,
        "valref still delivered in full via override");
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

/* When issuing a request fails, the walk aborts and kvs_treewalk_run()
 * returns -1 with the failure errno.
 */
static void test_request_failure (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_tree (bs, 4);
    struct collector c;
    struct kvs_treewalk *tw;
    struct kvs_treewalk_ops ops = test_ops;
    ops.valref_request = on_valref_request_fail;

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &ops, &c);
    errno = 0;
    ok (kvs_treewalk_run (tw) < 0 && errno == ENOMEM,
        "walk aborts with -1/ENOMEM when a request cannot be issued");
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

/* The descend predicate can prune a subtree: returning false for the 'sub'
 * dirref stops the walker from loading it, so 'sub/deep' is never visited.
 */
static void test_descend_prune (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_tree (bs, 4);
    struct collector c;
    struct kvs_treewalk *tw;
    struct kvs_treewalk_ops ops = test_ops;
    ops.descend = on_descend;

    collector_init (&c, h);
    c.prune_path = "sub";
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with descend prune ok");
    ok (c.descend_calls == 1, "descend called once for the 'sub' dirref (got %d)",
        c.descend_calls);
    ok (visited_has (&c, "sub"), "'sub' dirref still visited before prune");
    ok (!visited_has (&c, "sub/deep"),
        "'sub/deep' not visited (subtree pruned)");
    ok (c.values == 1, "only root-level val visited, deep val pruned (got %d)",
        c.values);
    ok (c.errors == 0, "prune reports no error (got %d)", c.errors);
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

/* With valref_noload set, the valref object is still reported through visit()
 * but no blob loads are issued and valref_done() is never called.
 */
static void test_valref_noload (flux_t *h, struct blobstore *bs)
{
    char *rootref = build_tree (bs, 4);
    struct collector c;
    struct kvs_treewalk *tw;
    struct kvs_treewalk_ops ops = test_ops;
    ops.valref_noload = true;
    ops.valref_request = on_valref_request; // must be ignored when noload set

    collector_init (&c, h);
    tw = kvs_treewalk_create (h, rootref, '/', 4, 0, &ops, &c);
    ok (kvs_treewalk_run (tw) == 0, "walk with valref_noload ok");
    ok (visited_has (&c, "big"), "valref object still visited");
    ok (c.valrefs_done == 0, "valref_done not called when noload set (got %d)",
        c.valrefs_done);
    ok (c.valref_requests == 0,
        "valref_request not called when noload set (got %d)",
        c.valref_requests);
    ok (c.valref_total_bytes == 0, "no valref blob bytes fetched (got %d)",
        c.valref_total_bytes);
    ok (c.errors == 0, "valref_noload reports no error (got %d)", c.errors);
    kvs_treewalk_destroy (tw);
    collector_fini (&c);
    free (rootref);
}

static void test_invalid_args (flux_t *h)
{
    struct collector c;
    collector_init (&c, h);
    ok (kvs_treewalk_create (NULL, "sha1-x", '/', 4, 0, &test_ops, &c) == NULL
        && errno == EINVAL,
        "create with NULL handle returns EINVAL");
    ok (kvs_treewalk_create (h, NULL, '/', 4, 0, &test_ops, &c) == NULL
        && errno == EINVAL,
        "create with NULL root returns EINVAL");
    ok (kvs_treewalk_create (h, "sha1-x", '/', 0, 0, &test_ops, &c) == NULL
        && errno == EINVAL,
        "create with window=0 returns EINVAL");
    lives_ok ({kvs_treewalk_destroy (NULL);},
        "kvs_treewalk_destroy (NULL) is a no-op");
    collector_fini (&c);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    struct blobstore bs;

    plan (NO_PLAN);

    if (!(bs.map = json_object ()))
        BAIL_OUT ("json_object failed");
    if (!(h = test_server_create (0, server_cb, &bs)))
        BAIL_OUT ("test_server_create failed");

    test_invalid_args (h);
    /* exercise window saturation behavior at small and large windows */
    test_basic_walk (h, &bs, 1);
    test_basic_walk (h, &bs, 2);
    test_basic_walk (h, &bs, 64);
    test_separator (h, &bs);
    test_inline_dir (h, &bs);
    test_valref_request_override (h, &bs);
    test_descend_prune (h, &bs);
    test_valref_noload (h, &bs);
    test_missing_dirref (h, &bs);
    test_missing_valref_blob (h, &bs);
    test_request_failure (h, &bs);

    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);
    json_decref (bs.map);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
