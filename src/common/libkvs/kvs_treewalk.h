/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _KVS_TREEWALK_H
#define _KVS_TREEWALK_H

#include <stdbool.h>
#include <flux/core.h>
#include <jansson.h>

/* Walk a KVS RFC 11 tree object hierarchy, depth first, starting from a root
 * blobref. The directory objects (dirref) and value blobs (valref) that must
 * be fetched from the content store are requested through a single bounded
 * window of in-flight content requests shared across the whole walk, driven
 * by one reactor. This hides the content round-trip latency that otherwise
 * dominates an offline walk of a large KVS (e.g. an instance with many
 * inactive jobs).
 *
 * The caller supplies callbacks (kvs_treewalk_ops) that implement the
 * per-object policy: what to do with a value, symlink, or completed valref,
 * and how to report an error. The walker owns the mechanism (queue, window,
 * dirref recursion, valref blob fan-out, completion ordering); the callbacks
 * own the meaning.
 *
 * Objects are visited in request completion order, which is deterministic but
 * not the same as a strict depth-first key order. Callbacks run one at a time
 * on the reactor thread, so a callback must NOT run a nested reactor or make a
 * blocking content RPC (that would deadlock); defer such work until after
 * kvs_treewalk_run() returns.
 */

struct kvs_treewalk;

/* Error category reported to the error callback. */
enum kvs_treewalk_error {
    KVS_TREEWALK_ERROR_INVALID,   // tree object failed validation
    KVS_TREEWALK_ERROR_BADCOUNT,  // dirref blobref count != 1
    KVS_TREEWALK_ERROR_LOAD,      // content load failed (errnum is set)
    KVS_TREEWALK_ERROR_DECODE,    // loaded dirref object could not be decoded
    KVS_TREEWALK_ERROR_NOTDIR,    // dirref object is not a directory
};

/* Result of one valref blob load, in valref index order. 'msg' is the
 * content.load response message (borrowed; valid only for the duration of the
 * valref_done callback) or NULL if the load failed, in which case 'errnum'
 * holds the error.
 */
struct kvs_treewalk_blob {
    const flux_msg_t *msg;
    int errnum;
};

struct kvs_treewalk_ops {
    /* Called once for every object as it is discovered, before dispatch.
     * Useful for verbose path listing.  May be NULL.
     */
    void (*visit) (void *arg, const char *path, json_t *treeobj);

    /* Called before a dirref subtree is loaded, with the dirref's (single)
     * blobref.  Return true to descend (load the directory object and recurse)
     * or false to prune the subtree (its children are not visited).  This lets
     * a caller walking several overlapping roots skip a subtree it has already
     * walked, avoiding repeated loads of shared interior nodes.  When NULL,
     * every dirref is descended.  Note that visit() has already fired for the
     * dirref object before this is called.
     */
    bool (*descend) (void *arg, const char *path, const char *blobref);

    /* If true, valref blobs are not fetched at all: the valref object is still
     * reported through visit() (from which its blobrefs are available), but
     * valref_request and valref_done are ignored and no blob load is issued.
     * Use when only the blobrefs matter (e.g. marking) and neither the content
     * nor the existence of each blob needs to be checked.
     */
    bool valref_noload;

    /* Inline value (val) and symbolic link (symlink) objects.  May be NULL.
     */
    void (*value) (void *arg, const char *path, json_t *treeobj);
    void (*symlink) (void *arg, const char *path, json_t *treeobj);

    /* Optional override for how each valref blob is fetched. When this
     * field is left NULL, the walker fetches blob content with
     * content_load_byblobref() and the walk's content flags. When supplied,
     * it is called to start the request for one blob and must return the
     * resulting future, or NULL with errno set on failure. Set this to send
     * a different request, e.g. to validate that a blob exists without
     * transferring its content.
     */
    flux_future_t *(*valref_request) (void *arg, const char *blobref);

    /* Called once when all blobs of a valref have completed, with results in
     * index order. 'treeobj' is the valref object (for access to blobrefs).
     * May be NULL.
     */
    void (*valref_done) (void *arg,
                         const char *path,
                         json_t *treeobj,
                         int count,
                         const struct kvs_treewalk_blob *blobs);

    /* Called when a dirref cannot be loaded, decoded, or validated.  The
     * subtree is pruned (its children are not visited).  The callback may
     * exit the process to make the error fatal, or return to continue.  May
     * be NULL (errors are then silently pruned).
     */
    void (*error) (void *arg,
                   const char *path,
                   enum kvs_treewalk_error error,
                   int errnum);
};

/* Create a tree walker.
 *  root_blobref  - blobref of the root directory object
 *  sep           - path separator joining directory entries ('/' or '.')
 *  window        - max concurrent content.load requests (> 0)
 *  content_flags - CONTENT_FLAG_* for the default loads (dirref and valref)
 *  ops, arg      - caller callbacks and opaque argument.  'arg' is stored in
 *                  the walker and passed back to each callback; the caller
 *                  must keep it valid until kvs_treewalk_destroy().
 * Returns walker on success, NULL on failure with errno set.
 */
struct kvs_treewalk *kvs_treewalk_create (flux_t *h,
                                          const char *root_blobref,
                                          char sep,
                                          int window,
                                          int content_flags,
                                          const struct kvs_treewalk_ops *ops,
                                          void *arg);

/* Run the walk to completion on the handle's reactor.
 * Returns 0 on success, -1 on a reactor error. Per-object errors are
 * reported through the error callback, not here.
 */
int kvs_treewalk_run (struct kvs_treewalk *tw);

void kvs_treewalk_destroy (struct kvs_treewalk *tw);

#endif /* !_KVS_TREEWALK_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
