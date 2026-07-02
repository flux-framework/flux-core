/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* kvs_treewalk.c - walk a KVS treeobj hierarchy with a bounded content.load
 * window. See kvs_treewalk.h for the model.
 *
 * The walk maintains one FIFO work queue of content.load requests not yet
 * issued, and keeps up to 'window' of them in flight at once. Each completed
 * dirref load decodes a directory object and feeds its entries back into the
 * walk (enqueuing the next level of dirref/valref loads); each completed
 * valref blob is accumulated until its value is whole, then handed to the
 * caller. The window is shared by dirref and valref loads alike, so the walk
 * stays saturated regardless of tree shape. When the queue drains and no
 * loads remain in flight, the reactor is stopped.
 *
 * inline dir, val, and symlink objects need no RPC and are handled
 * synchronously as they are discovered.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <flux/core.h>

#include "src/common/libcontent/content.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "treeobj.h"
#include "kvs_treewalk.h"

/* Accumulator shared by the blob loads of one valref. valref_done can only
 * be called once every blob has completed.
 */
struct valref_obj {
    char *path;
    json_t *treeobj;            // borrowed reference to the valref object
    struct kvs_treewalk_blob *blobs;  // [count] results in index order
    int count;
    int remaining;              // blobs not yet completed
};

/* One pending content.load: either a dirref directory object (vobj == NULL)
 * or one blob of a valref value (vobj != NULL).
 */
struct load_op {
    struct kvs_treewalk *tw;
    char *blobref;
    char *path;                 // dirref: full path of the directory
    struct valref_obj *vobj;    // valref: shared accumulator, else NULL
    int blob_index;             // valref: index of this blob within the value
};

struct kvs_treewalk {
    flux_t *h;
    char *root_blobref;
    char sep;
    int window;
    int content_flags;
    struct kvs_treewalk_ops ops;
    void *arg;
    zlistx_t *workq;           // FIFO of struct load_op * awaiting issue
    int in_flight;             // content.load requests currently outstanding
    int errnum;                // first fatal (internal) error, else 0
};

static void classify (struct kvs_treewalk *tw,
                      const char *path,
                      json_t *treeobj);
static void treewalk_pump (struct kvs_treewalk *tw);

/* Record a fatal internal error (OOM or request-send failure) that aborts the
 * whole walk. The first error wins. The walk unwinds: classify()/walk_dir()
 * stop descending, and load_continuation() stops the reactor once it sees
 * this set. kvs_treewalk_run() then returns -1 with this errno.
 */
static void treewalk_fatal (struct kvs_treewalk *tw, int errnum)
{
    if (tw->errnum == 0)
        tw->errnum = errnum;
}

static void report_error (struct kvs_treewalk *tw,
                          const char *path,
                          enum kvs_treewalk_error error,
                          int errnum)
{
    if (tw->ops.error)
        tw->ops.error (tw->arg, path, error, errnum);
}

static void valref_obj_destroy (struct valref_obj *vobj)
{
    if (vobj) {
        int saved_errno = errno;
        if (vobj->blobs) {
            for (int i = 0; i < vobj->count; i++)
                flux_msg_decref (vobj->blobs[i].msg);
            free (vobj->blobs);
        }
        json_decref (vobj->treeobj);
        free (vobj->path);
        free (vobj);
        errno = saved_errno;
    }
}

static struct valref_obj *valref_obj_create (const char *path,
                                             json_t *treeobj,
                                             int count)
{
    struct valref_obj *vobj;

    if (!(vobj = calloc (1, sizeof (*vobj)))
        || !(vobj->path = strdup (path))
        || !(vobj->blobs = calloc (count, sizeof (vobj->blobs[0])))) {
        valref_obj_destroy (vobj);
        return NULL;
    }
    vobj->treeobj = json_incref (treeobj);
    vobj->count = count;
    vobj->remaining = count;
    return vobj;
}

static void load_op_destroy (struct load_op *op)
{
    if (op) {
        int saved_errno = errno;
        free (op->blobref);
        free (op->path);
        free (op);
        errno = saved_errno;
    }
}

/* zlistx_destructor_fn footprint */
static void load_op_destructor (void **item)
{
    if (item) {
        load_op_destroy (*item);
        *item = NULL;
    }
}

/* Enqueue one content.load (issued later by treewalk_pump()). 'vobj' is NULL
 * for a dirref directory object, or the shared accumulator for a valref blob.
 */
static void enqueue_load (struct kvs_treewalk *tw,
                          const char *blobref,
                          const char *path,
                          struct valref_obj *vobj,
                          int blob_index)
{
    struct load_op *op;

    if (!(op = calloc (1, sizeof (*op)))
        || !(op->blobref = strdup (blobref))
        || (path && !(op->path = strdup (path)))) {
        load_op_destroy (op);
        treewalk_fatal (tw, errno);
        return;
    }
    op->tw = tw;
    op->vobj = vobj;
    op->blob_index = blob_index;
    if (!zlistx_add_end (tw->workq, op)) {
        load_op_destroy (op);
        treewalk_fatal (tw, ENOMEM);
    }
}

/* Walk the entries of a directory object, joining each child onto 'path'.
 * The root is reached with path="", so its children use bare names; deeper
 * children are "dir<sep>name".
 */
static void walk_dir (struct kvs_treewalk *tw,
                      const char *path,
                      json_t *treeobj)
{
    json_t *dict = treeobj_get_data (treeobj);
    const char *name;
    json_t *entry;

    json_object_foreach (dict, name, entry) {
        char *newpath;
        if (tw->errnum) // a fatal error aborted the walk
            return;
        if (*path == '\0')
            newpath = strdup (name);
        else if (asprintf (&newpath, "%s%c%s", path, tw->sep, name) < 0)
            newpath = NULL;
        if (!newpath) {
            treewalk_fatal (tw, ENOMEM);
            return;
        }
        classify (tw, newpath, entry);
        free (newpath);
    }
}

/* Classify one object discovered at 'path' and either handle it now (inline
 * types) or enqueue the content.load(s) needed to dump it (dirref, valref).
 */
static void classify (struct kvs_treewalk *tw,
                      const char *path,
                      json_t *treeobj)
{
    if (treeobj_validate (treeobj) < 0) {
        report_error (tw, path, KVS_TREEWALK_ERROR_INVALID, 0);
        return;
    }
    if (tw->ops.visit)
        tw->ops.visit (tw->arg, path, treeobj);

    if (treeobj_is_dir (treeobj)) {
        walk_dir (tw, path, treeobj);
    }
    else if (treeobj_is_dirref (treeobj)) {
        const char *blobref;
        if (treeobj_get_count (treeobj) != 1) {
            report_error (tw, path, KVS_TREEWALK_ERROR_BADCOUNT, 0);
            return;
        }
        blobref = treeobj_get_blobref (treeobj, 0);
        /* Let the caller prune an already-walked subtree (see ops.descend). */
        if (tw->ops.descend && !tw->ops.descend (tw->arg, path, blobref))
            return;
        enqueue_load (tw, blobref, path, NULL, 0);
    }
    else if (treeobj_is_valref (treeobj)) {
        /* count >= 1: treeobj_validate() above rejects an empty valref. */
        int count = treeobj_get_count (treeobj);
        struct valref_obj *vobj;

        /* The caller only wants the blobrefs (reported via visit above), not
         * the blob content or existence: skip the loads entirely.
         */
        if (tw->ops.valref_noload)
            return;

        if (!(vobj = valref_obj_create (path, treeobj, count))) {
            treewalk_fatal (tw, errno);
            return;
        }
        for (int i = 0; i < count; i++)
            enqueue_load (tw,
                          treeobj_get_blobref (treeobj, i),
                          NULL,
                          vobj,
                          i);
    }
    else if (treeobj_is_val (treeobj)) {
        if (tw->ops.value)
            tw->ops.value (tw->arg, path, treeobj);
    }
    else if (treeobj_is_symlink (treeobj)) {
        if (tw->ops.symlink)
            tw->ops.symlink (tw->arg, path, treeobj);
    }
}

/* A dirref directory object finished loading: decode it and walk its entries.
 * On a load/decode/type error, prune this subtree (its children are never
 * discovered).
 */
static void dirref_complete (struct load_op *op, flux_future_t *f)
{
    struct kvs_treewalk *tw = op->tw;
    const void *buf;
    size_t buflen;
    json_t *treeobj;

    if (content_load_get (f, &buf, &buflen) < 0) {
        report_error (tw, op->path, KVS_TREEWALK_ERROR_LOAD, errno);
        return;
    }
    if (!(treeobj = treeobj_decodeb (buf, buflen))) {
        report_error (tw, op->path, KVS_TREEWALK_ERROR_DECODE, 0);
        return;
    }
    if (!treeobj_is_dir (treeobj)) {
        report_error (tw, op->path, KVS_TREEWALK_ERROR_NOTDIR, 0);
        json_decref (treeobj);
        return;
    }
    walk_dir (tw, op->path, treeobj);
    json_decref (treeobj);
}

/* One blob of a valref finished loading: stash its result. When the last
 * blob arrives, hand the whole value to the caller.
 */
static void valref_complete (struct load_op *op, flux_future_t *f)
{
    struct kvs_treewalk *tw = op->tw;
    struct valref_obj *vobj = op->vobj;
    const flux_msg_t *msg;

    if (flux_future_get (f, (const void **)&msg) < 0) {
        vobj->blobs[op->blob_index].errnum = errno;
    }
    else {
        vobj->blobs[op->blob_index].msg = flux_msg_incref (msg);
        vobj->blobs[op->blob_index].errnum = 0;
    }
    if (--vobj->remaining == 0) {
        if (tw->ops.valref_done)
            tw->ops.valref_done (tw->arg,
                                 vobj->path,
                                 vobj->treeobj,
                                 vobj->count,
                                 vobj->blobs);
        valref_obj_destroy (vobj);
    }
}

/* Common continuation for every content.load: dispatch by op type, refill the
 * window, and stop the reactor once the walk is complete.
 */
static void load_continuation (flux_future_t *f, void *arg)
{
    struct load_op *op = arg;
    struct kvs_treewalk *tw = op->tw;

    tw->in_flight--;
    if (op->vobj)
        valref_complete (op, f);
    else
        dirref_complete (op, f);
    flux_future_destroy (f);
    load_op_destroy (op);

    treewalk_pump (tw);

    /* Stop on normal completion (queue drained, nothing in flight) or as soon
     * as a fatal error has aborted the walk.
     */
    if (tw->errnum || (tw->in_flight == 0 && zlistx_size (tw->workq) == 0))
        flux_reactor_stop (flux_get_reactor (tw->h));
}

/* Issue one queued load. valref blobs may use a caller-supplied request (e.g.
 * to validate existence instead of transferring content); everything else
 * uses content_load_byblobref() with the walk's content flags.
 */
static flux_future_t *issue_load (struct kvs_treewalk *tw, struct load_op *op)
{
    if (op->vobj && tw->ops.valref_request)
        return tw->ops.valref_request (tw->arg, op->blobref);
    return content_load_byblobref (tw->h, op->blobref, tw->content_flags);
}

/* Issue queued content.load requests until the window is full or the queue
 * drains.
 */
static void treewalk_pump (struct kvs_treewalk *tw)
{
    struct load_op *op;

    while (!tw->errnum
           && tw->in_flight < tw->window
           && (op = zlistx_detach (tw->workq, NULL))) {
        flux_future_t *f;

        if (!(f = issue_load (tw, op))
            || flux_future_then (f, -1, load_continuation, op) < 0) {
            treewalk_fatal (tw, errno);
            flux_future_destroy (f);
            load_op_destroy (op);
            return;
        }
        tw->in_flight++;
    }
}

int kvs_treewalk_run (struct kvs_treewalk *tw)
{
    if (!tw) {
        errno = EINVAL;
        return -1;
    }
    /* The root is a blob that decodes to a directory object, i.e. an implicit
     * dirref with path "".
     */
    enqueue_load (tw, tw->root_blobref, "", NULL, 0);
    treewalk_pump (tw);
    /* Run the reactor only if a load is actually in flight.  If the initial
     * pump failed (e.g. OOM) nothing is outstanding, so running would block
     * forever; fall through to the error return instead.
     */
    if (tw->in_flight > 0
        && flux_reactor_run (flux_get_reactor (tw->h), 0) < 0)
        return -1;
    if (tw->errnum) {
        errno = tw->errnum;
        return -1;
    }
    return 0;
}

struct kvs_treewalk *kvs_treewalk_create (flux_t *h,
                                          const char *root_blobref,
                                          char sep,
                                          int window,
                                          int content_flags,
                                          const struct kvs_treewalk_ops *ops,
                                          void *arg)
{
    struct kvs_treewalk *tw;

    if (!h || !root_blobref || window <= 0 || !ops) {
        errno = EINVAL;
        return NULL;
    }
    if (!(tw = calloc (1, sizeof (*tw)))
        || !(tw->root_blobref = strdup (root_blobref))
        || !(tw->workq = zlistx_new ()))
        goto error;
    zlistx_set_destructor (tw->workq, load_op_destructor);
    tw->h = h;
    tw->sep = sep;
    tw->window = window;
    tw->content_flags = content_flags;
    tw->ops = *ops;
    tw->arg = arg;
    return tw;
error:
    kvs_treewalk_destroy (tw);
    return NULL;
}

void kvs_treewalk_destroy (struct kvs_treewalk *tw)
{
    if (tw) {
        int saved_errno = errno;
        zlistx_destroy (&tw->workq);
        free (tw->root_blobref);
        free (tw);
        errno = saved_errno;
    }
}

/*
 * vi:ts=4 sw=4 expandtab
 */
