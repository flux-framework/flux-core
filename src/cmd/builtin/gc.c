/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libkvs/kvs_treewalk.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libcontent/content.h"
#include "ccan/str/str.h"

#include "builtin.h"

/* Max concurrent content.load requests kept in flight while walking the KVS
 * to mark reachable blobs.  The walk pipelines interior-node (dirref) loads
 * through this window to hide content round-trip latency, which otherwise
 * dominates a mark of a large KVS (e.g. an instance with many inactive jobs).
 *
 * flux-gc runs against a live instance, so the default is kept low (matching
 * flux-dump) to avoid competing with real traffic for the content backing
 * store during a naive run.  An off-peak run (e.g. from cron) can raise it
 * with --maxreqs; mark throughput saturates by a window of ~16, so a modest
 * value already recovers the full speedup.
 */
#define GC_WALK_WINDOW_DEFAULT 2

static int verbose = 0;
static int dry_run = 0;
static int no_cache = 0;
static int walk_window = GC_WALK_WINDOW_DEFAULT;
static int test_delay_after_list = 0;

static int64_t horizon_epoch = 0;
static int mark_batch_size = 100;
static int sweep_batch_size = 1000;

/* Set of treeobj blobrefs already walked during this run, used to prune
 * redundant subtree traversal.  The marked roots (retained checkpoints,
 * the live primary root, and private namespace roots) share large
 * subtrees, so without this we would reload and re-walk the same
 * treeobjs many times per run.
 *
 * Membership means "this run already marked this blob AND fully walked
 * its subtree," so a re-encounter can be skipped entirely.  The signal
 * MUST be local visited-state, not the backing-store epoch: a treeobj
 * stored by a recent commit has epoch >= H while still referencing
 * unchanged children with epoch < H (epoch is not propagated to
 * deduplicated children), so epoch >= H does not imply the subtree is
 * marked.  Only treeobj blobrefs (roots and dirrefs that we load and
 * recurse into) are tracked; raw valref leaves are cheap to re-mark and
 * are not recorded, bounding memory to the interior-node count.
 */
static json_t *visited;

/* Async batched marking.
 *
 * kvs_treewalk callbacks run on the reactor thread and must not block or run a
 * nested reactor, so marking cannot use a blocking RPC from inside the walk.
 * Instead we accumulate reachable blobrefs into a batch and, when it fills,
 * fire a content-backing.mark RPC without waiting; mark_reap() collects each
 * response on the same reactor the walk is driving.  This bounds memory (only
 * a batch plus the in-flight requests are held) without a synchronous stall.
 * After the walk, mark_drain() flushes the final partial batch and waits for
 * the outstanding requests to complete.
 */
struct marker {
    flux_t *h;
    json_t *batch;      // json array of blobref strings not yet sent
    int batch_count;
    int outstanding;    // mark RPCs sent but not yet reaped
    bool draining;      // set after the walk; lets mark_reap stop the reactor
    int errnum;         // errno of first failure, else 0
};

/* Record blobref as walked.  Returns 1 if it was already present (caller
 * should skip it), 0 if newly recorded, -1 on error.
 */
static int visited_check_add (const char *blobref)
{
    if (json_object_get (visited, blobref))
        return 1;
    if (json_object_set_new (visited, blobref, json_true ()) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int gc_info (flux_t *h, int64_t threshold, int64_t *current_epoch, int *candidates)
{
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(f = flux_rpc_pack (h,
                             "content-backing.gc-info",
                             0,
                             0,
                             "{s:I}",
                             "epoch", threshold)))
        goto done;
    if (flux_rpc_get_unpack (f,
                             "{s:I s:i}",
                             "current_epoch", current_epoch,
                             "candidates", candidates) < 0)
        log_msg_exit ("failed to fetch current epoch: %s",
                      future_strerror (f, errno));
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Reap one completed content-backing.mark RPC.  Record the first error so the
 * caller can abort before sweeping (a mark failure must never be followed by a
 * sweep).  When draining after the walk, stop the reactor once the last
 * request has been collected.
 */
static void mark_reap (flux_future_t *f, void *arg)
{
    struct marker *m = arg;
    int marked;

    if (flux_rpc_get_unpack (f, "{s:i}", "marked", &marked) < 0) {
        if (m->errnum == 0)
            m->errnum = errno;
        log_err ("mark failed: %s", future_strerror (f, errno));
    }
    else if (verbose > 1)
        log_msg ("marked %d blobs", marked);

    flux_future_destroy (f);
    m->outstanding--;

    if (m->draining && m->outstanding == 0)
        flux_reactor_stop (flux_get_reactor (m->h));
}

/* Send the current batch as one content-backing.mark RPC without waiting for
 * the response (mark_reap collects it on the reactor).  A fresh batch array is
 * installed so accumulation can continue.  No-op on an empty batch.
 */
static void mark_flush (struct marker *m)
{
    flux_future_t *f;
    json_t *batch;

    if (m->errnum || m->batch_count == 0)
        return;
    if (!(f = flux_rpc_pack (m->h,
                             "content-backing.mark",
                             0,
                             0,
                             "{s:I s:O}",
                             "epoch", horizon_epoch,
                             "hashes", m->batch))
        || flux_future_then (f, -1, mark_reap, m) < 0) {
        m->errnum = errno;
        flux_future_destroy (f);
        log_err ("cannot send mark request");
        return;
    }
    m->outstanding++;
    /* Replace the batch with an empty array; the sent one is owned by 'f'. */
    if (!(batch = json_array ())) {
        m->errnum = ENOMEM;
        return;
    }
    json_decref (m->batch);
    m->batch = batch;
    m->batch_count = 0;
}

/* Queue one blobref to be marked, flushing the batch when it fills. */
static void mark_add (struct marker *m, const char *blobref)
{
    json_t *s;

    if (m->errnum)
        return;
    if (!(s = json_string (blobref))) {
        m->errnum = ENOMEM;
        return;
    }
    /* json_array_append_new steals 's' even on failure, so no decref here. */
    if (json_array_append_new (m->batch, s) < 0) {
        m->errnum = ENOMEM;
        return;
    }
    if (++m->batch_count >= mark_batch_size)
        mark_flush (m);
}

/* kvs_treewalk visit callback: mark the leaf blobrefs of a valref.  With
 * valref_noload set, the valref is reported here but its blobs are never
 * loaded (marking needs only the blobrefs, which the treeobj already holds).
 * dirref blobs are marked in mark_descend(); dir/val/symlink have no blob of
 * their own to mark.
 */
static void mark_visit (void *arg, const char *path, json_t *treeobj)
{
    struct marker *m = arg;

    if (treeobj_is_valref (treeobj)) {
        int count = treeobj_get_count (treeobj);
        for (int i = 0; i < count; i++)
            mark_add (m, treeobj_get_blobref (treeobj, i));
    }
}

/* kvs_treewalk descend predicate: mark a dirref's blob and decide whether to
 * walk its subtree.  Roots share large subtrees (retained checkpoints, the
 * live primary root, private namespace roots), so 'visited' prunes a subtree
 * already walked this run -- both to avoid reloading its interior nodes and to
 * mark each shared dirref only once.
 */
static bool mark_descend (void *arg, const char *path, const char *blobref)
{
    struct marker *m = arg;
    int v;

    if (m->errnum)
        return false;
    if ((v = visited_check_add (blobref)) != 0) {
        if (v < 0)
            m->errnum = errno;
        return false;  // already walked (or error): prune
    }
    mark_add (m, blobref);
    return true;
}

/* kvs_treewalk error callback: a dirref could not be loaded, decoded, or
 * validated.  A mark that cannot see a whole subtree must not be followed by a
 * sweep, so this is fatal (the subtree's blobs would otherwise be swept).
 */
static void mark_error (void *arg,
                        const char *path,
                        enum kvs_treewalk_error error,
                        int errnum)
{
    switch (error) {
        case KVS_TREEWALK_ERROR_INVALID:
            log_msg_exit ("%s: invalid tree object", path);
        case KVS_TREEWALK_ERROR_BADCOUNT:
            log_msg_exit ("%s: dirref blobref count is not 1", path);
        case KVS_TREEWALK_ERROR_LOAD:
            log_errn_exit (errnum, "%s: cannot load dirref", path);
        case KVS_TREEWALK_ERROR_DECODE:
            log_msg_exit ("%s: cannot decode dirref treeobj", path);
        case KVS_TREEWALK_ERROR_NOTDIR:
            log_msg_exit ("%s: dirref references non-directory", path);
    }
}

static const struct kvs_treewalk_ops mark_ops = {
    .visit = mark_visit,
    .descend = mark_descend,
    .valref_noload = true,
    .error = mark_error,
};

/* Mark one root and everything reachable from it.  The root object itself is
 * not reported by the walk (it is loaded internally), so mark its blobref here
 * and use 'visited' to skip a root already covered by an earlier root.
 */
static int mark_root (struct marker *m, const char *blobref)
{
    struct kvs_treewalk *tw;
    int content_flags = no_cache ? CONTENT_FLAG_CACHE_BYPASS : 0;
    int v;

    if ((v = visited_check_add (blobref)) != 0)
        return v < 0 ? -1 : 0;  // already walked this run (roots overlap)

    mark_add (m, blobref);
    if (m->errnum) {
        errno = m->errnum;
        return -1;
    }

    if (!(tw = kvs_treewalk_create (m->h,
                                    blobref,
                                    '.',
                                    walk_window,
                                    content_flags,
                                    &mark_ops,
                                    m)))
        return -1;
    /* Per-object errors go through mark_error (fatal); a reactor error or a
     * failure to load the root returns -1 here.
     */
    if (kvs_treewalk_run (tw) < 0) {
        kvs_treewalk_destroy (tw);
        return -1;
    }
    kvs_treewalk_destroy (tw);
    if (m->errnum) {
        errno = m->errnum;
        return -1;
    }
    return 0;
}

static int enumerate_checkpoint_roots (flux_t *h, json_t *roots_array)
{
    flux_future_t *f = NULL;
    json_t *checkpoints = NULL;
    size_t index;
    json_t *cp;
    int rc = -1;
    int count = 0;

    if (!(f = flux_rpc (h, "content-backing.checkpoint-get", NULL, 0, 0)))
        goto done;
    if (flux_rpc_get_unpack (f, "{s:o}", "value", &checkpoints) < 0) {
        /* No checkpoints yet (ENOENT) is OK - just no roots to mark */
        if (errno == ENOENT) {
            if (verbose)
                log_msg ("no checkpoints found");
            rc = 0;
            goto done;
        }
        log_msg_exit ("checkpoint-get: %s", future_strerror (f, errno));
    }

    if (!json_is_array (checkpoints)) {
        errno = EPROTO;
        goto done;
    }

    json_array_foreach (checkpoints, index, cp) {
        const char *rootref;
        json_t *root_str;

        if (kvs_checkpoint_parse_rootref (cp, &rootref) < 0) {
            log_err ("failed to parse checkpoint rootref");
            goto done;
        }

        if (!(root_str = json_string (rootref))
            || json_array_append_new (roots_array, root_str) < 0) {
            errno = ENOMEM;
            goto done;
        }

        count++;
    }

    if (verbose)
        log_msg ("enumerated %d checkpoint roots", count);

    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int enumerate_private_namespace_roots (flux_t *h, json_t *roots_array)
{
    flux_future_t *f = NULL;
    json_t *namespaces = NULL;
    size_t index;
    json_t *ns;
    int rc = -1;
    int count = 0;

    if (!(f = flux_rpc (h, "kvs.namespace-list", NULL, 0, 0)))
        goto done;
    if (flux_rpc_get_unpack (f, "{s:o}", "namespaces", &namespaces) < 0) {
        /* KVS not loaded: there are no live private namespaces, so
         * checkpoints alone are correct (mirroring flux-dump --checkpoint).
         */
        if (errno == ENOSYS || errno == ENOENT) {
            if (verbose)
                log_msg ("kvs not loaded; skipping live private namespace roots");
            rc = 0;
            goto done;
        }
        log_msg_exit ("kvs.namespace-list: %s", future_strerror (f, errno));
    }

    if (!json_is_array (namespaces)) {
        errno = EPROTO;
        goto done;
    }

    /* Widen the list->getroot race window so a test can destroy a namespace
     * after it has been listed but before its root is fetched.
     */
    if (test_delay_after_list > 0)
        sleep (test_delay_after_list);

    json_array_foreach (namespaces, index, ns) {
        const char *namespace;
        int owner;
        int flags;
        flux_future_t *getroot_f = NULL;
        const char *rootref;
        json_t *root_str;

        if (json_unpack (ns, "{s:s s:i s:i}",
                         "namespace", &namespace,
                         "owner", &owner,
                         "flags", &flags) < 0) {
            errno = EPROTO;
            goto done;
        }

        /* Only mark private (per-job) namespaces here; the primary
         * namespace is covered by checkpoints and the live primary root.
         */
        if (streq (namespace, KVS_PRIMARY_NAMESPACE))
            continue;

        /* Get the root for this namespace */
        if (!(getroot_f = flux_rpc_pack (h,
                                         "kvs.getroot",
                                         0,
                                         0,
                                         "{s:s}",
                                         "namespace", namespace)))
            goto done;

        if (flux_rpc_get_unpack (getroot_f, "{s:s}", "rootref", &rootref) < 0) {
            /* The namespace may have been destroyed between namespace-list
             * and getroot.  A vanished namespace returns ENOENT, or ENOTSUP
             * from the rank 0 kvs (which has no root to look up); either way
             * there is nothing to mark, so skip it.
             */
            if (errno == ENOENT || errno == ENOTSUP) {
                flux_future_destroy (getroot_f);
                continue;
            }
            log_msg_exit ("kvs.getroot %s: %s",
                          namespace,
                          future_strerror (getroot_f, errno));
        }

        if (!(root_str = json_string (rootref))
            || json_array_append_new (roots_array, root_str) < 0) {
            flux_future_destroy (getroot_f);
            errno = ENOMEM;
            goto done;
        }

        flux_future_destroy (getroot_f);
        count++;
    }

    if (verbose)
        log_msg ("enumerated %d private namespace roots", count);

    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Enumerate the live primary root.
 *
 * This protects data that has been re-referenced into the primary tree
 * without being re-stored, the prime example being a completed job whose
 * private namespace root is grafted into the primary tree (a
 * content-addressed treeobj snapshot via flux_kvs_copy()) and then
 * destroyed.  The grafted subtree's blobs keep their original (possibly
 * < H) epoch, and between the graft and the next primary checkpoint they
 * are reachable from neither a checkpoint root nor a live private
 * namespace root.  Marking the live primary root closes that window.
 *
 * Ordering invariant: this MUST be enumerated AFTER the private
 * namespace roots.  Let N be the time private roots are snapshotted and
 * P the time the primary root is read.  A job completes by grafting (at
 * c) then deleting its namespace (at d > c).  It is unprotected only if
 * d <= N (namespace gone before we snapshot privates) AND c >= P (graft
 * absent from the primary root we read); since c < d this requires
 * P <= c < d <= N, i.e. P < N.  Enumerating privates first guarantees
 * N <= P, making that impossible.
 */
static int enumerate_primary_live_root (flux_t *h, json_t *roots_array)
{
    flux_future_t *f = NULL;
    const char *rootref;
    json_t *root_str;
    int rc = -1;

    if (!(f = flux_rpc_pack (h,
                             "kvs.getroot",
                             0,
                             0,
                             "{s:s}",
                             "namespace", KVS_PRIMARY_NAMESPACE)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:s}", "rootref", &rootref) < 0) {
        /* KVS not loaded: there are no live roots, so checkpoints alone
         * are correct (mirroring flux-dump --checkpoint).
         */
        if (errno == ENOSYS || errno == ENOENT) {
            if (verbose)
                log_msg ("kvs not loaded; skipping live primary root");
            rc = 0;
            goto done;
        }
        log_msg_exit ("kvs.getroot %s: %s",
                      KVS_PRIMARY_NAMESPACE,
                      future_strerror (f, errno));
    }
    if (!(root_str = json_string (rootref))
        || json_array_append_new (roots_array, root_str) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (verbose)
        log_msg ("enumerated live primary root");
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

/* Flush the final partial batch and wait for every outstanding mark RPC to
 * complete.  Called once, after all roots have been walked; the walk itself
 * cannot drain because mark_reap must not stop the reactor mid-walk.
 */
static int mark_drain (struct marker *m)
{
    m->draining = true;
    mark_flush (m);  // send the trailing partial batch, if any
    if (m->outstanding > 0
        && flux_reactor_run (flux_get_reactor (m->h), 0) < 0) {
        if (m->errnum == 0)
            m->errnum = errno;
    }
    if (m->errnum) {
        errno = m->errnum;
        return -1;
    }
    return 0;
}

static int mark_all_roots (flux_t *h, json_t *roots_array)
{
    struct marker m = { .h = h };
    size_t index;
    json_t *root;
    int rc = -1;

    if (!(m.batch = json_array ())) {
        log_err ("failed to create mark batch");
        goto done;
    }

    json_array_foreach (roots_array, index, root) {
        const char *rootref = json_string_value (root);

        if (!rootref) {
            log_err ("invalid root at index %zu", index);
            goto done;
        }

        if (verbose)
            log_msg ("marking from root %s", rootref);

        if (mark_root (&m, rootref) < 0) {
            log_err ("failed to mark from root %s: %s", rootref, strerror (errno));
            goto done;
        }
    }

    /* Flush the trailing batch and wait for all mark RPCs to complete. */
    if (mark_drain (&m) < 0)
        goto done;

    if (verbose)
        log_msg ("mark phase complete");

    rc = 0;
done:
    json_decref (m.batch);
    return rc;
}

static int sweep_blobs (flux_t *h)
{
    int total_deleted = 0;
    int remaining = 1;  // Non-zero to enter loop

    if (verbose)
        log_msg ("starting sweep phase");

    while (remaining > 0) {
        flux_future_t *f = NULL;
        int deleted;

        if (!(f = flux_rpc_pack (h,
                                 "content-backing.sweep",
                                 0,
                                 0,
                                 "{s:I s:i}",
                                 "epoch", horizon_epoch,
                                 "batch_size", sweep_batch_size)))
            return -1;

        if (flux_rpc_get_unpack (f,
                                 "{s:i s:i}",
                                 "deleted", &deleted,
                                 "remaining", &remaining) < 0)
            log_msg_exit ("sweep failed: %s", future_strerror (f, errno));

        total_deleted += deleted;

        if (verbose > 1)
            log_msg ("swept %d blobs, %d remaining", deleted, remaining);

        flux_future_destroy (f);
    }

    if (verbose)
        log_msg ("sweep complete: deleted %d blobs", total_deleted);

    return 0;
}

int cmd_gc (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int64_t current_epoch;
    int candidates;
    int optindex = optparse_option_index (p);
    json_t *roots = NULL;

    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }

    verbose = optparse_get_int (p, "verbose", 0);
    dry_run = optparse_hasopt (p, "dry-run");
    no_cache = optparse_hasopt (p, "no-cache");
    walk_window = optparse_get_int (p, "maxreqs", GC_WALK_WINDOW_DEFAULT);
    if (walk_window <= 0)
        log_err_exit ("invalid value for maxreqs");
    test_delay_after_list = optparse_get_int (p, "test-delay-after-list", 0);

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    /* Get current epoch and freeze it as the horizon */
    if (gc_info (h, 0, &current_epoch, &candidates) < 0)
        log_err_exit ("gc-info");

    horizon_epoch = current_epoch;

    if (verbose)
        log_msg ("froze epoch at %jd", (intmax_t)horizon_epoch);

    if (dry_run) {
        int dry_candidates;
        if (gc_info (h, horizon_epoch, &current_epoch, &dry_candidates) < 0)
            log_err_exit ("gc-info");
        log_msg ("current_epoch: %jd", (intmax_t)current_epoch);
        log_msg ("reclaimable blobs (epoch < %jd): %d",
                 (intmax_t)horizon_epoch,
                 dry_candidates);
        flux_close (h);
        return 0;
    }

    /* Enumerate roots */
    if (!(roots = json_array ()))
        log_err_exit ("failed to create roots array");
    if (!(visited = json_object ()))
        log_err_exit ("failed to create visited set");

    if (enumerate_checkpoint_roots (h, roots) < 0)
        log_err_exit ("failed to enumerate checkpoint roots");

    if (enumerate_private_namespace_roots (h, roots) < 0)
        log_err_exit ("failed to enumerate private namespace roots");

    /* MUST follow private namespace enumeration (see
     * enumerate_primary_live_root() for the ordering invariant).
     */
    if (enumerate_primary_live_root (h, roots) < 0)
        log_err_exit ("failed to enumerate live primary root");

    if (verbose)
        log_msg ("enumerated %zu total roots", json_array_size (roots));

    /* Mark phase */
    if (mark_all_roots (h, roots) < 0)
        log_err_exit ("mark phase failed");

    /* Stop after the mark phase (testing only).  This is the same state a
     * crash between mark and sweep would leave: the mark phase is complete
     * and durable, and the next run re-marks from scratch before sweeping,
     * so stopping here must leave no data at risk.
     */
    if (optparse_hasopt (p, "test-mark-only")) {
        log_msg ("stopping after mark phase (test)");
        json_decref (roots);
        json_decref (visited);
        flux_close (h);
        return 0;
    }

    /* Sweep phase */
    if (sweep_blobs (h) < 0)
        log_err_exit ("sweep phase failed");

    log_msg ("gc complete");
    json_decref (roots);
    json_decref (visited);
    flux_close (h);
    return 0;
}

static struct optparse_option gc_opts[] = {
    {
        .name = "dry-run",
        .key = 'n',
        .has_arg = 0,
        .usage = "Report reclaimable blobs without deleting",
    },
    {
        .name = "no-cache",
        .has_arg = 0,
        .usage = "Bypass broker content cache",
    },
    {
        .name = "maxreqs",
        .has_arg = 1,
        .arginfo = "N",
        .usage = "Increase number of concurrent content requests during the"
                 " mark phase (default 2)",
    },
    {
        .name = "verbose",
        .key = 'v',
        .has_arg = 0,
        .usage = "Increase verbosity",
    },
    {
        .name = "test-mark-only",
        .has_arg = 0,
        .flags = OPTPARSE_OPT_HIDDEN,
        .usage = "Run the mark phase only, skipping sweep (testing only)",
    },
    {
        .name = "test-delay-after-list",
        .has_arg = 1,
        .arginfo = "SECONDS",
        .flags = OPTPARSE_OPT_HIDDEN,
        .usage = "Delay after listing namespaces, before fetching their"
                 " roots (testing only)",
    },
    OPTPARSE_TABLE_END
};

int subcommand_gc_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
                                  "gc",
                                  cmd_gc,
                                  "[OPTIONS]",
                                  "Online garbage collection for KVS content",
                                  0,
                                  gc_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
