/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* JSON directory object:
 * list of key-value pairs where key is a name, value is a dirent
 *
 * JSON dirent objects:
 * object containing one key-value pair where key is one of
 * "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", "LINKVAL", and value is a SHA1
 * hash key into ctx->store (FILEREF, DIRREF), an actual directory, file
 * (value), or link target JSON object (FILEVAL, DIRVAL, LINKVAL).
 *
 * For example, consider KVS containing:
 * a="foo"
 * b="bar"
 * c.d="baz"
 * X -> c.d
 *
 * Root directory:
 * {"a":{"FILEREF":"f1d2d2f924e986ac86fdf7b36c94bcdf32beec15"},
 *  "b":{"FILEREF","8714e0ef31edb00e33683f575274379955b3526c"},
 *  "c":{"DIRREF","6eadd3a778e410597c85d74c287a57ad66071a45"},
 *  "X":{"LINKVAL","c.d"}}
 *
 * Deep copy of root directory:
 * {"a":{"FILEVAL":"foo"},
 *  "b":{"FILEVAL","bar"},
 *  "c":{"DIRVAL",{"d":{"FILEVAL":"baz"}}},
 *  "X":{"LINKVAL","c.d"}}
 *
 * On LINKVAL's:
 * - target is always fully qualified key name
 * - links are always followed in path traversal of intermediate directories
 * - for kvs_get, terminal links are only followed if 'readlink' flag is set
 * - for kvs_put, terminal links are never followed
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/time.h>
#include <czmq.h>
#include <fnmatch.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

#include "waitqueue.h"
#include "proto.h"

typedef char href_t[41];

/* Large values are stored in dirents by reference; small values by value.
 *  (-1 = all by reference, 0 = all by value)
 */
#define LARGE_VAL (sizeof (href_t) + 1)

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

/* Expire hobj from cache after 'max_lastuse_age' heartbeats.
 */
const int max_lastuse_age = 5;

/* Coalesce commits that arrive within min_commit_window seconds
 * of previous update.
 */
const double min_commit_window = 1E-3;

/* Include root directory in kvs.setroot event.
 */
const bool event_includes_rootdir = true;

/* Hash object in ctx->store by SHA1 key.
 */
typedef struct {
    waitqueue_t *waitlist;  /* waiters for HOBJ_COMPLETE */
    json_object *o;         /* value object */
    int size;               /* size of value for stats, est by serialization */
    int lastuse_epoch;      /* time of last use for cache expiry */
    enum {
        HOBJ_COMPLETE,      /* nothing in progress, o != NULL */
        HOBJ_INCOMPLETE,    /* load in progress, o == NULL */
        HOBJ_DIRTY,         /* store in progress, o != NULL */
    } state;
} hobj_t;

typedef struct {
    json_object *dirents;   /* hash key (e.g. a.b.c) => dirent */
    flux_msg_t *request;    /* request message */
    struct timespec t0;     /* commit begin timestamp */
    char *fence;            /* fence name, if applicable */
    enum {
        COMMIT_PUT,         /* put is still appending items to this commit */
        COMMIT_STORE,       /* stores running for objs referenced by commit */
        COMMIT_UPSTREAM,    /* upstream commit running */
        COMMIT_MASTER,      /* (master only) commit ASAP */
        COMMIT_FENCE,       /* (master only) commit upon fence count==nprocs */
    } state;
} commit_t;

typedef struct {
    int nprocs;
    int count;
} fence_t;

/* Statistics gathered for kvs.stats.get, etc.
 */
typedef struct {
    tstat_t commit_time;
    tstat_t commit_merges;
    tstat_t get_time;
    tstat_t put_time;
    tstat_t fence_time;
    int faults;
    int noop_stores;
} stats_t;

typedef struct {
    zhash_t *store;         /* hash SHA1 => hobj_t */
    href_t rootdir;         /* current root SHA1 */
    int rootseq;            /* current root version (for ordering) */
    zhash_t *commits;       /* hash of pending put/commits by sender name */
    zhash_t *fences;        /* hash of pending fences by name (master only) */
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    stats_t stats;
    flux_t h;
    bool master;            /* for now minimize flux_get_rank() calls */
    int epoch;              /* tracks current heartbeat epoch */
    struct timespec commit_time; /* time of most recent commit */
    bool commit_timer_armed;
    struct json_tokener *tok;
    flux_watcher_t *commit_timer;
} ctx_t;

enum {
    KVS_GET_DIRVAL = 1,
    KVS_GET_FILEVAL = 2,
};

static int setroot_event_send (ctx_t *ctx, const char *fence);
static void commit_respond (ctx_t *ctx, const flux_msg_t *msg,
                            const char *sender,
                            const char *rootdir, int rootseq);
static void load_complete (ctx_t *ctx, const href_t ref, json_object *o);
static void store_complete (ctx_t *ctx, href_t ref);

static void commit_timeout_handler (flux_reactor_t *r, flux_watcher_t *w,
                                    int revents, void *arg);

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    if (ctx) {
        zhash_destroy (&ctx->store);
        zhash_destroy (&ctx->commits);
        zhash_destroy (&ctx->fences);
        if (ctx->watchlist)
            wait_queue_destroy (ctx->watchlist);
        flux_watcher_destroy (ctx->commit_timer);
        if (ctx->tok)
            json_tokener_free (ctx->tok);
        free (ctx);
    }
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "kvssrv");
    uint32_t rank;
    flux_reactor_t *r;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(r = flux_get_reactor (h)))
            goto error;
        ctx->store = zhash_new ();
        ctx->commits = zhash_new ();
        ctx->fences = zhash_new ();
        ctx->watchlist = wait_queue_create ();
        if (!ctx->store || !ctx->commits || !ctx->fences || !ctx->watchlist) {
            errno = ENOMEM;
            goto error;
        }
        ctx->h = h;
        if (flux_get_rank (h, &rank) < 0)
            goto error;
        if (rank == 0)
            ctx->master = true;
        if (!(ctx->commit_timer = flux_timer_watcher_create (r,
                                                min_commit_window, 0.,
                                                commit_timeout_handler, ctx)))
            goto error;
        if (!(ctx->tok = json_tokener_new ()))
            oom ();
        flux_aux_set (h, "kvssrv", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static bool store_by_reference (json_object *o)
{
    if (LARGE_VAL == -1)
        return true;
    if (strlen (json_object_to_json_string (o)) >= LARGE_VAL)
        return true;
    return false;
}

static json_object *dirent_create (char *type, void *arg)
{
    json_object *o = util_json_object_new_object ();
    bool valid_type = false;

    if (!strcmp (type, "FILEREF") || !strcmp (type, "DIRREF")) {
        char *ref = arg;

        util_json_object_add_string (o, type, ref);
        valid_type = true;
    } else if (!strcmp (type, "FILEVAL") || !strcmp (type, "DIRVAL")
                                         || !strcmp (type, "LINKVAL")) {
        json_object *val = arg;

        if (val)
            json_object_get (val);
        else
            val = util_json_object_new_object ();
        json_object_object_add (o, type, val);
        valid_type = true;
    }
    assert (valid_type == true);

    return o;
}

static commit_t *commit_create (void)
{
    commit_t *c = xzmalloc (sizeof (*c));
    return c;
}

static void commit_destroy (commit_t *c)
{
    if (c) {
        if (c->dirents)
            json_object_put (c->dirents);
        if (c->fence)
            free (c->fence);
        flux_msg_destroy (c->request);
        free (c);
    }
}

static void commit_add (commit_t *c, const char *key, json_object *dirent)
{
    if (!c->dirents)
        c->dirents = util_json_object_new_object ();
    json_object_object_add (c->dirents, key, dirent);
}

static fence_t *fence_create (int nprocs)
{
    fence_t *f = xzmalloc (sizeof (*f));
    f->nprocs = nprocs;
    return f;
}

static void fence_destroy (fence_t *f)
{
    free (f);
}

static bool hobj_update (ctx_t *ctx, hobj_t *hp, json_object *o)
{
    bool noop = false ;
    if (hp->o) {
        json_object_put (o);
        noop = true;
    } else {
        hp->o = o;
        hp->size = util_json_size (hp->o);
    }
    return !noop;
}

static hobj_t *hobj_create (ctx_t *ctx, json_object *o)
{
    hobj_t *hp = xzmalloc (sizeof (*hp));

    hp->waitlist = wait_queue_create ();
    if (o) {
        hp->o = o;
        hp->size = util_json_size (hp->o);
    }
    return hp;
}

static void hobj_destroy (hobj_t *hp)
{
    if (hp->o)
        json_object_put (hp->o);
    wait_queue_destroy (hp->waitlist);
    free (hp);
}

static void load_completion (flux_rpc_t *rpc, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    json_object_iter iter;

    if (flux_rpc_get (rpc, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    if (!(in = json_tokener_parse_ex (ctx->tok, json_str, strlen (json_str)))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        json_tokener_reset (ctx->tok);
        goto done;
    }
    json_object_object_foreachC (in, iter) {
        Jget (iter.val);
        load_complete (ctx, iter.key, iter.val); /* href -> value */
    }
done:
    flux_rpc_destroy (rpc);
    Jput (in);
}

static int load_request_send (ctx_t *ctx, const href_t ref)
{
    JSON in = Jnew ();
    flux_rpc_t *rpc = NULL;

    Jadd_obj (in, ref, NULL);
    if (!(rpc = flux_rpc (ctx->h, "kvs.load", Jtostr (in),
                                            FLUX_NODEID_UPSTREAM, 0)))
        goto error;
    if (flux_rpc_then (rpc, load_completion, ctx) < 0)
        goto error;
    Jput (in);
    return 0;
error:
    Jput (in);
    flux_rpc_destroy (rpc);
    return -1;
}

static bool load (ctx_t *ctx, const href_t ref, wait_t *wait, json_object **op)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);
    bool done = true;

    if (hp)
        hp->lastuse_epoch = ctx->epoch;
    if (ctx->master) {
        /* FIXME: probably should handle this "can't happen" situation.
         */
        if (!hp || hp->state != HOBJ_COMPLETE)
            msg_exit ("dangling ref %s", ref);
    }

    /* Create an incomplete hash entry if none found.
     */
    if (!hp) {
        hp = hobj_create (ctx, NULL);
        zhash_insert (ctx->store, ref, hp);
        zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
        hp->state = HOBJ_INCOMPLETE;
        if (load_request_send (ctx, ref) < 0)
            flux_log_error (ctx->h, "load_request_send");
        ctx->stats.faults++;
    }
    /* If hash entry is incomplete (either created above or earlier),
     * arrange to stall caller if wait_t was provided.
     */
    if (hp->state == HOBJ_INCOMPLETE) {
        if (wait)
            wait_addqueue (hp->waitlist, wait);
        done = false; /* stall */
    }

    if (done) {
        FASSERT (ctx->h, hp != NULL);
        FASSERT (ctx->h, hp->o != NULL);
        if (op)
            *op = hp->o;
    }
    return done;
}

/* Store the results of a load request.
 */
static void load_complete (ctx_t *ctx, const href_t ref, json_object *o)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);

    if (hp) {
        (void)hobj_update (ctx, hp, o);
        if (hp->state == HOBJ_INCOMPLETE) {
            hp->state = HOBJ_COMPLETE;
            wait_runqueue (hp->waitlist);
        }
    } else {
        hp = hobj_create (ctx, o);
        hp->state = HOBJ_COMPLETE;
        zhash_update (ctx->store, ref, hp);
        zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
    }
}

static void store_completion (flux_rpc_t *rpc, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    json_object_iter iter;

    if (flux_rpc_get (rpc, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (in, iter) {
        store_complete (ctx, iter.key);
    }
done:
    Jput (in);
    flux_rpc_destroy (rpc);
}

static int store_request_send (ctx_t *ctx, const href_t ref, json_object *val)
{
    JSON in = Jnew ();
    flux_rpc_t *rpc;

    Jadd_obj (in, ref, val);
    if (!(rpc = flux_rpc (ctx->h, "kvs.store", Jtostr (in),
                                        FLUX_NODEID_UPSTREAM, 0)))
        goto error;
    if (flux_rpc_then (rpc, store_completion, ctx) < 0)
        goto error;
    Jput (in);
    return 0;
error:
    flux_rpc_destroy (rpc);
    Jput (in);
    return -1;
}

/* N.B. cache may have expired (if not dirty) so consider object
 * states of missing and incomplete to be "not dirty" in this context.
 */
static bool store_isdirty (ctx_t *ctx, const href_t ref, wait_t *wait)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);

    if (hp && hp->state == HOBJ_DIRTY) {
        if (wait)
            wait_addqueue (hp->waitlist, wait);
        return true;
    }
    return false;
}

static void store (ctx_t *ctx, json_object *o, href_t ref)
{
    hobj_t *hp;
    zdigest_t *zd;
    const char *s = json_object_to_json_string (o);
    const char *zdstr;

    if (!(zd = zdigest_new ()))
        oom ();
    zdigest_update (zd, (byte *)s, strlen (s));
    zdstr = zdigest_string (zd);
    assert (zdstr != NULL); /* indicates czmq built without crypto? */
    assert (sizeof (href_t) == strlen (zdstr) + 1);
    memcpy (ref, zdstr, strlen (zdstr) + 1);
    zdigest_destroy (&zd);

    if ((hp = zhash_lookup (ctx->store, ref))) {
        if (!hobj_update (ctx, hp, o))
            ctx->stats.noop_stores++;
    } else {
        hp = hobj_create (ctx, o);
        zhash_insert (ctx->store, ref, hp);
        zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
        if (ctx->master) {
            hp->state = HOBJ_COMPLETE;
        } else {
            hp->state = HOBJ_DIRTY;
            store_request_send (ctx, ref, o);
        }
    }
}

/* Update a hash item after its upstream store has completed.
 */
static void store_complete (ctx_t *ctx, href_t ref)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);

    if (hp && hp->state == HOBJ_DIRTY) {
        hp->state = HOBJ_COMPLETE;
        wait_runqueue (hp->waitlist);
    }
}

static void setroot (ctx_t *ctx, const char *rootdir, int rootseq)
{
    if (rootseq == 0 || rootseq > ctx->rootseq) {
        memcpy (ctx->rootdir, rootdir, sizeof (href_t));
        ctx->rootseq = rootseq;
        wait_runqueue (ctx->watchlist);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
}

static json_object *copydir (json_object *dir)
{
    json_object *cpy;
    json_object_iter iter;

    if (!(cpy = json_object_new_object ()))
        oom ();
    json_object_object_foreachC (dir, iter) {
        json_object_get (iter.val);
        json_object_object_add (cpy, iter.key, iter.val);
    }
    return cpy;
}

static void commit_unroll (ctx_t *ctx, json_object *dir)
{
    json_object_iter iter;
    json_object *subdir;
    href_t ref;
    zhash_t *new;
    zlist_t *keys;
    char *key, *refcpy;

    if (!(new = zhash_new ()))
        oom ();
    json_object_object_foreachC (dir, iter) {
        if (json_object_object_get_ex (iter.val, "DIRVAL", &subdir)) {
            commit_unroll (ctx, subdir); /* depth first */
            json_object_get (subdir);
            store (ctx, subdir, ref);
            zhash_insert (new, iter.key, xstrdup (ref));
        }
    }
    /* hash contains name => SHA1 ref */
    if (!(keys = zhash_keys (new)))
        oom ();
    while ((key = zlist_pop (keys))) {
        refcpy = zhash_lookup (new, key);
        FASSERT (ctx->h, refcpy != NULL);
        json_object_object_del (dir, key);
        json_object_object_add (dir, key, dirent_create ("DIRREF", refcpy));
        free (refcpy);
        free (key);
    }
    zlist_destroy (&keys);
    zhash_destroy (&new);
}

/* link (key, dirent) into directory 'dir'.
 */
static void commit_link_dirent (ctx_t *ctx, json_object *dir,
                                const char *key, json_object *dirent)
{
    char *next, *name = xstrdup (key);
    json_object *o, *subdir = NULL, *subdirent;

    /* This is the first part of a key with multiple path components.
     * Make sure that it is a directory in DIRVAL form, then recurse
     * on the remaining path components.
     */
    if ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!json_object_object_get_ex (dir, name, &subdirent)) {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto done;
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else if (json_object_object_get_ex (subdirent, "DIRVAL", &o)) {
            subdir = o;
        } else if (json_object_object_get_ex (subdirent, "DIRREF", &o)) {
            bool stall = !load (ctx, json_object_get_string (o), NULL, &subdir);
            FASSERT (ctx->h, stall == false);
            json_object_object_del (dir, name);
            subdir = copydir (subdir);/* do not corrupt store by modify orig. */
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto done;
            json_object_object_del (dir, name);
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        }
        commit_link_dirent (ctx, subdir, next, dirent);

    /* This is the final path component of the key.  Add it to the directory.
     */
    } else {
        json_object_object_del (dir, name);
        if (dirent) {
            json_object_get (dirent);
            json_object_object_add (dir, name, dirent);
        }
    }
done:
    free (name);
}

static json_object *commit_apply_start (ctx_t *ctx)
{
    json_object *rootdir = NULL;
    bool stall = !load (ctx, ctx->rootdir, NULL, &rootdir);

    FASSERT (ctx->h, !stall);
    FASSERT (ctx->h, rootdir != NULL);
    return copydir (rootdir); /* do not corrupt store by modifying orig. */
}

static void commit_apply_dirents (ctx_t *ctx, json_object *rootcpy, commit_t *c)
{
    json_object_iter iter;

    if (c->dirents) {
        json_object_object_foreachC (c->dirents, iter) {
            commit_link_dirent (ctx, rootcpy, iter.key, iter.val);
        }
    }
}

static bool commit_apply_finish (ctx_t *ctx, json_object *rootcpy)
{
    href_t ref;

    commit_unroll (ctx, rootcpy);
    store (ctx, rootcpy, ref);
    if (!strcmp (ref, ctx->rootdir))
        return false;
    setroot (ctx, ref, ctx->rootseq + 1);
    monotime (&ctx->commit_time);
    return true;
}

/* Apply a single commit.
 * N.B. This is only used during initialization
 */
static bool commit_apply_one (ctx_t *ctx, commit_t *c)
{
    json_object *rootcpy;

    if (!c->dirents)
        return false;
    rootcpy = commit_apply_start (ctx);
    commit_apply_dirents (ctx, rootcpy, c);
    return commit_apply_finish (ctx, rootcpy);
}

/* Apply all the commits found in ctx->commits in COMMIT_MASTER state.
 * Return the number of commits applied.
 */
static int commit_apply_all (ctx_t *ctx)
{
    json_object *rootcpy;
    char *key;
    zlist_t *keys;
    commit_t *c;
    int count = 0;

    rootcpy = commit_apply_start (ctx);

    if (!(keys = zhash_keys (ctx->commits)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        c = zhash_lookup (ctx->commits, key);
        assert (c != NULL);
        if (c->state == COMMIT_MASTER) {
            commit_apply_dirents (ctx, rootcpy, c);
            count++;
        }
        key = zlist_next (keys);
    }

    if (commit_apply_finish (ctx, rootcpy))
        setroot_event_send (ctx, NULL);

    while ((key = zlist_pop (keys))) {
        c = zhash_lookup (ctx->commits, key);
        assert (c != NULL);
        if (c->state == COMMIT_MASTER) {
            FASSERT (ctx->h, c->request != NULL);
            commit_respond (ctx, c->request, key, NULL, 0);
            if (monotime_isset (c->t0))
                tstat_push (&ctx->stats.commit_time,  monotime_since (c->t0));
            zhash_delete (ctx->commits, key);
        }
        free (key);
    }
    zlist_destroy (&keys);
    tstat_push (&ctx->stats.commit_merges, count);
    return count;
}

/* Fence.
 */
static void commit_apply_fence (ctx_t *ctx, const char *name)
{
    json_object *rootcpy;
    char *key;
    zlist_t *keys;
    commit_t *c;

    rootcpy = commit_apply_start (ctx);

    if (!(keys = zhash_keys (ctx->commits)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        if ((c = zhash_lookup (ctx->commits, key)) && c->state == COMMIT_FENCE
                                              && !strcmp (name, c->fence)) {
            commit_apply_dirents (ctx, rootcpy, c);
        }
        key = zlist_next (keys);
    }

    (void)commit_apply_finish (ctx, rootcpy);
    setroot_event_send (ctx, name);

    while ((key = zlist_pop (keys))) {
        if ((c = zhash_lookup (ctx->commits, key))
                                            && c->state == COMMIT_FENCE
                                              && !strcmp (name, c->fence)) {
            commit_respond (ctx, c->request, key, NULL, 0);
            zhash_delete (ctx->commits, key);
        }
        free (key);
    }
    zlist_destroy (&keys);
}

static void commit_complete_fence (ctx_t *ctx, const char *fence,
                                   const char *rootdir, int rootseq)
{
    char *key;
    zlist_t *keys;
    commit_t *c;

    zhash_delete (ctx->fences, fence);

    if (!(keys = zhash_keys (ctx->commits)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        if ((c = zhash_lookup (ctx->commits, key)) && c->fence
                                              && !strcmp (fence, c->fence)) {
            commit_respond (ctx, c->request, key, rootdir, rootseq);
            zhash_delete (ctx->commits, key);
        }
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);

}

/* This timeout fires when the window for coalescing commits
 * has expired.
 */
static void commit_timeout_handler (flux_reactor_t *r, flux_watcher_t *w,
                                    int revents, void *arg)
{
    ctx_t *ctx = arg;
    int count;

    ctx->commit_timer_armed = false;

    count = commit_apply_all (ctx);
    if (count > 1) {
        flux_log (ctx->h, LOG_DEBUG, "%s: coalesced %d commits",
                  __FUNCTION__, count);
    }
}

static void load_request_cb (flux_t h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON out = Jnew ();
    JSON val;
    json_object_iter iter;
    wait_t *wait = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (!(wait = wait_create (h, w, msg, load_request_cb, arg)))
        goto done;
    json_object_object_foreachC (in, iter) {
        if (!load (ctx, iter.key, wait, &val))
            goto stall;
        Jadd_obj (out, iter.key, val);
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    if (wait)
        wait_destroy (wait, NULL);
stall:
    Jput (in);
    Jput (out);
}

static void store_request_cb (flux_t h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON out = Jnew ();
    json_object_iter iter;
    href_t href;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = json_tokener_parse_ex (ctx->tok, json_str, strlen (json_str)))) {
        json_tokener_reset (ctx->tok);
        errno = EPROTO;
        goto done;
    }
    json_object_object_foreachC (in, iter) {
        Jget (iter.val);
        store (ctx, iter.val, href);
        Jadd_obj (out, iter.key, NULL);
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
    Jput (out);
}

static int expire_cache (ctx_t *ctx, int thresh)
{
    zlist_t *keys;
    char *key;
    hobj_t *hp;
    int expcount = 0;

    if (!(keys = zhash_keys (ctx->store)))
        oom ();
    while ((key = zlist_pop (keys))) {
        if ((hp = zhash_lookup (ctx->store, key))) {
            if (hp->lastuse_epoch == 0)
                hp->lastuse_epoch = ctx->epoch;
            if (hp->state == HOBJ_COMPLETE
              && (thresh == 0 || ctx->epoch - hp->lastuse_epoch  > thresh)) {
                zhash_delete (ctx->store, key);
                expcount++;
            }
        }
        free (key);
    }
    zlist_destroy (&keys);
    return expcount;
}

static void dropcache_request_cb (flux_t h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    int size, expcount = 0;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    if (ctx->master) {
        errno = EINVAL;
        goto done;
    }
    size = zhash_size (ctx->store);
    expcount = expire_cache (ctx, 0);
    flux_log (h, LOG_ALERT, "dropped %d of %d cache entries", expcount, size);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void dropcache_event_cb (flux_t h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    int size, expcount = 0;

    if (ctx->master)
        return;
    if (flux_event_decode (msg, NULL, NULL) < 0)
        return;
    size = zhash_size (ctx->store);
    expcount = expire_cache (ctx, 0);
    flux_log (h, LOG_ALERT, "dropped %d of %d cache entries", expcount, size);
}

static void heartbeat_cb (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        return;
    }
    /* "touch" objects involved in watched keys */
    if (ctx->epoch - ctx->watchlist_lastrun_epoch > max_lastuse_age) {
        wait_runqueue (ctx->watchlist);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
    /* "touch" root */
    (void)load (ctx, ctx->rootdir, NULL, NULL);

    expire_cache (ctx, max_lastuse_age);
}

/* Get dirent containing requested key.
 */
static bool walk (ctx_t *ctx, json_object *root, const char *path,
                  json_object **direntp, wait_t *wait, bool readlink, int depth)
{
    char *cpy = xstrdup (path);
    char *next, *name = cpy;
    const char *ref;
    const char *link;
    json_object *dirent = NULL;
    json_object *dir = root;

    depth++;

    /* walk directories */
    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!json_object_object_get_ex (dir, name, &dirent))
            goto error;
        if (util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (ctx, root, link, &dirent, wait, false, depth))
                goto stall;
            if (!dirent)
                goto error;
        }
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (!load (ctx, ref, wait, &dir))
                goto stall;

        } else if (json_object_object_get_ex (dirent, "DIRVAL", &dir)) {
            /* N.B. in current code, directories are never stored by value */
            msg_exit ("%s: unexpected DIRVAL: path=%s name=%s: dirent=%s ",
                      __FUNCTION__, path, name, Jtostr (dirent));
        } else if ((util_json_object_get_string (dirent, "FILEREF", NULL) == 0
                 || json_object_object_get_ex (dirent, "FILEVAL", NULL))) {
            errno = ENOTDIR;
            goto error;
        } else {
            msg_exit ("%s: unknown dirent type: path=%s name=%s: dirent=%s ",
                      __FUNCTION__, path, name, Jtostr (dirent));
        }
        name = next;
    }
    /* now terminal path component */
    if (json_object_object_get_ex (dir, name, &dirent) &&
        util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
        if (!readlink) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (ctx, root, link, &dirent, wait, readlink, depth))
                goto stall;
        }
    }
    free (cpy);
    *direntp = dirent;
    return true;
error:
    free (cpy);
    *direntp = NULL;
    return true;
stall:
    free (cpy);
    return false;
}

static bool lookup (ctx_t *ctx, json_object *root, wait_t *wait,
                    bool dir, bool readlink, const char *name,
                    json_object **valp, int *ep)
{
    json_object *vp, *dirent, *val = NULL;
    const char *ref;
    bool isdir = false;
    int errnum = 0;

    if (!strcmp (name, ".")) { /* special case root */
        if (!dir) {
            errnum = EISDIR;
            goto done;
        }
        val = root;
        isdir = true;
    } else {
        if (!walk (ctx, root, name, &dirent, wait, readlink, 0))
            goto stall;
        if (!dirent) {
            //errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (!dir) {
                errnum = EISDIR;
                goto done;
            }
            if (!load (ctx, ref, wait, &val))
                goto stall;
            isdir = true;
        } else if (util_json_object_get_string (dirent, "FILEREF", &ref) == 0) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (dir) {
                errnum = ENOTDIR;
                goto done;
            }
            if (!load (ctx, ref, wait, &val))
                goto stall;
        } else if (json_object_object_get_ex (dirent, "DIRVAL", &vp)) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (!dir) {
                errnum = EISDIR;
                goto done;
            }
            val = vp;
            isdir = true;
        } else if (json_object_object_get_ex (dirent, "FILEVAL", &vp)) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (dir) {
                errnum = ENOTDIR;
                goto done;
            }
            val = vp;
        } else if (json_object_object_get_ex (dirent, "LINKVAL", &vp)) {
            FASSERT (ctx->h, readlink == true); /* walk() ensures this */
            FASSERT (ctx->h, dir == false); /* dir && readlink should never happen */
            val = vp;
        } else
            msg_exit ("%s: corrupt internal storage", __FUNCTION__);
    }
    /* val now contains the requested object */
    if (isdir)
        val = copydir (val);
    else 
        json_object_get (val);
done:
    *valp = val;
    if (errnum != 0)
        *ep = errnum;
    return true;
stall:
    return false;
}

static void get_request_cb (flux_t h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON out = NULL;
    bool dir = false;
    bool link = false;
    const char *key;
    char *sender = NULL;
    JSON root, val;
    wait_t *wait = NULL;
    int lookup_errnum = 0;
    double msec;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_tget_dec (in, &key, &dir, &link) < 0)
        goto done;
    if (!(wait = wait_create (h, w, msg, get_request_cb, arg)))
        goto done;
    if (!load (ctx, ctx->rootdir, wait, &root))
        goto stall;
    if (!lookup (ctx, root, wait, dir, link, key, &val, &lookup_errnum))
        goto stall;
    if (lookup_errnum != 0) {
        errno = lookup_errnum;
        goto done;
    }
    if (!(out = kp_rget_enc (key, val)))
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    wait_destroy (wait, &msec);
    tstat_push (&ctx->stats.get_time, msec);
stall:
    Jput (in);
    Jput (out);
    if (sender)
        free (sender);
}

static void watch_request_cb (flux_t h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON in2 = NULL;
    JSON out = NULL;
    JSON root;
    JSON oval, val = NULL;
    flux_msg_t *cpy = NULL;
    const char *key;
    bool dir = false, first = false;
    bool link = false, once = false;
    int lookup_errnum = 0;
    wait_t *wait = NULL;
    wait_t *watcher = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_twatch_dec (in, &key, &oval, &once, &first, &dir, &link) < 0)
        goto done;
    if (!(wait = wait_create (h, w, msg, watch_request_cb, arg)))
        goto done;
    if (!load (ctx, ctx->rootdir, wait, &root))
        goto stall;
    if (!lookup (ctx, root, wait, dir, link, key, &val, &lookup_errnum))
        goto stall;
    if (lookup_errnum) {
        errno = lookup_errnum;
        goto done;
    }
    /* Value changed or this is the initial request, so prepare a reply.
     */
    if (first || !util_json_match (val, oval)) {
        if (!(out = kp_rwatch_enc (key, Jget (val))))
            goto done;
    }
    /* No reply sent or this is a multi-response watch request.
     * Arrange to wait on ctx->watchlist for each new commit.
     * Reconstruct the payload with 'first' set false, and updated value.
     */
    if (!out || !once) {
        if (!(cpy = flux_msg_copy (msg, false)))
            goto done;
        if (!(in2 = kp_twatch_enc (key, Jget (val), once, false, dir, link)))
            goto done;
        if (flux_msg_set_payload_json (cpy, Jtostr (in2)) < 0)
            goto done;
        if (!(watcher = wait_create (h, w, cpy, watch_request_cb, arg)))
            goto done;
        wait_addqueue (ctx->watchlist, watcher);
    }
    rc = 0;
done:
    if (rc < 0 || out != NULL) {
        if (flux_respond (h, msg, rc < 0 ? errno : 0,
                                  rc < 0 ? NULL : Jtostr (out)) < 0)
            flux_log_error (h, "%s", __FUNCTION__);
    }
    wait_destroy (wait, NULL);
stall:
    Jput (val);
    Jput (in);
    Jput (in2);
    Jput (out);
    flux_msg_destroy (cpy);
}

typedef struct {
    const char *key;
    char *sender;
} unwatch_param_t;

static bool unwatch_cmp (const flux_msg_t *msg, void *arg)
{
    unwatch_param_t *p = arg;
    char *sender = NULL;
    JSON o = NULL;
    const char *topic, *json_str;
    bool match = false;

    if (flux_request_decode (msg, &topic, &json_str) < 0)
        goto done;
    if (strcmp (topic, "kvs.watch") != 0)
        goto done;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (strcmp (sender, p->sender) != 0)
        goto done;
    if (!(o = Jfromstr (json_str)))
        goto done;
    if (!json_object_object_get_ex (o, p->key, NULL))
        goto done;
    match = true;
done:
    if (sender)
        free (sender);
    Jput (o);
    return match;
}

static void unwatch_request_cb (flux_t h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    unwatch_param_t p = { NULL, NULL };
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_tunwatch_dec (in, &p.key) < 0)
        goto done;
    if (flux_msg_get_route_first (msg, &p.sender) < 0)
        goto done;
    if (wait_destroy_match (ctx->watchlist, unwatch_cmp, &p) < 0)
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
    if (p.sender)
        free (p.sender);
}

static void put_request_cb (flux_t h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    href_t ref;
    const char *key;
    JSON val = NULL;
    bool dir = false;
    bool link = false;
    struct timespec t0;
    char *sender = NULL;
    commit_t *commit;
    int rc = -1;

    monotime (&t0);
    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_tput_dec (in, &key, &val, &link, &dir) < 0)
        goto done;
    if (!(commit = zhash_lookup (ctx->commits, sender))) {
        commit = commit_create ();
        commit->state = COMMIT_PUT;
        zhash_insert (ctx->commits, sender, commit);
        zhash_freefn (ctx->commits, sender, (zhash_free_fn *)commit_destroy);
    }
    if (commit->state != COMMIT_PUT) { /* commit already in progress */
        errno = EINVAL;
        goto done;
    }
    if (json_object_get_type (val) == json_type_null) {
        if (dir) {
            JSON empty_dir = Jnew ();
            store (ctx, empty_dir, ref);
            commit_add (commit, key, dirent_create ("DIRREF", ref));
        } else
            commit_add (commit, key, NULL);
    } else if (link) {
        commit_add (commit, key, dirent_create ("LINKVAL", val));
    } else if (store_by_reference (val)) {
        store (ctx, Jget (val), ref);
        commit_add (commit, key, dirent_create ("FILEREF", ref));
    } else {
        commit_add (commit, key, dirent_create ("FILEVAL", val));
    }
    tstat_push (&ctx->stats.put_time, monotime_since (t0));
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
    if (sender)
        free (sender);
}

static int commit_complete (ctx_t *ctx, flux_rpc_t *rpc)
{
    const char *json_str;
    JSON in = NULL;
    const char *rootdir, *sender;
    int rootseq;
    commit_t *commit;
    int rc = -1;

    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rcommit_dec (in, &rootseq, &rootdir, &sender) < 0)
        goto done;

    /* Update the cache on this node.
     * If we have already advanced here or beyond, this is a no-op.
     */
    setroot (ctx, rootdir, rootseq);

    /* Find the original commit_t associated with this request and respond.
     */
    if ((commit = zhash_lookup (ctx->commits, sender))) {
        FASSERT (ctx->h, commit->state == COMMIT_UPSTREAM);
        FASSERT (ctx->h, commit->request != NULL);
        commit_respond (ctx, commit->request, sender, rootdir, rootseq);
        if (monotime_isset (commit->t0))
            tstat_push (&ctx->stats.commit_time,  monotime_since (commit->t0));
        zhash_delete (ctx->commits, sender);
    }
    rc = 0;
done:
    Jput (in);
    return rc;
}

static void commit_completion (flux_rpc_t *rpc, void *arg)
{
    ctx_t *ctx = arg;

    if (commit_complete (ctx, rpc) < 0)
        flux_log_error (ctx->h, "commit_complete");
    flux_rpc_destroy (rpc);
}

/* Send a new commit request upstream.
 * When the response is recieved, we will look up this commit_t by sender
 * and respond to c->request (if set).
 */
static int send_upstream_commit (ctx_t *ctx, commit_t *c, const char *sender,
                                 const char *fence, int nprocs)
{
    flux_rpc_t *rpc = NULL;
    JSON in;

    if (!(in = kp_tcommit_enc (sender, c->dirents, fence, nprocs)))
        goto error;
    if (!(rpc = flux_rpc (ctx->h, "kvs.commit",
                                    Jtostr (in), FLUX_NODEID_UPSTREAM, 0)))
        goto error;
    if (flux_rpc_then (rpc, commit_completion, ctx) < 0)
        goto error;

    Jput (c->dirents);
    c->dirents = NULL;
    Jput (in);
    return 0;
error:
    flux_log_error (ctx->h, "%s", __FUNCTION__);
    flux_rpc_destroy (rpc);
    Jput (in);
    return -1;
}

/* If commit contains references to dirty hash entries (store in progress),
 * arrange for commit_request_cb () to be restarted once these are all clean.
 * Return true if handler should stall; false if there are no dirty references.
 */
static bool commit_dirty (ctx_t *ctx, commit_t *c, wait_t *wait)
{
    const char *ref;
    json_object_iter iter;
    bool dirty = false;

    if (c->dirents) {
        json_object_object_foreachC (c->dirents, iter) {
            if (!iter.val)
                continue;
            if ((util_json_object_get_string (iter.val, "FILEREF", &ref) == 0
              || util_json_object_get_string (iter.val, "DIRREF", &ref) == 0)
                                            && store_isdirty (ctx, ref, wait)) {
                dirty = true;
            }
        }
    }

    return dirty;
}

static void commit_respond (ctx_t *ctx, const flux_msg_t *msg,
                            const char *sender,
                            const char *rootdir, int rootseq)
{
    JSON out = NULL;
    int rc = -1;

    if (!msg)
        return;
    if (!rootdir) {
        rootdir = ctx->rootdir;
        rootseq = ctx->rootseq;
    }
    if (!(out = kp_rcommit_enc (rootseq, rootdir, sender)))
        goto done;
    rc = 0;
done:
    if (flux_respond (ctx->h, msg, rc < 0 ? errno : 0, Jtostr (out)) < 0)
        flux_log_error (ctx->h, "%s", __FUNCTION__);
    Jput (out);
}

static void commit_request_cb (flux_t h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON dirents = NULL;
    commit_t *c = NULL;
    wait_t *wait;
    char *sender = NULL;
    const char *arg_sender;
    int nprocs;
    const char *fence = NULL;
    bool internal = false;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (kp_tcommit_dec (in, &arg_sender, &dirents, &fence, &nprocs) < 0)
        goto error;

    /* Commits generated internally will contain .arg_sender.  If present,
     * we should ignore the true sender and use it to hash the commit.
     */
    if (arg_sender) {
        free (sender);
        sender = xstrdup (arg_sender);
        internal = true;
    }
    /* issue 109:  fence is terminated by an event that may allow a client
     * to send a new commit/fence before internal state of old fence is
     * cleared from upstream ranks.
     */
    if ((c = zhash_lookup (ctx->commits, sender))
                    && c->state == COMMIT_UPSTREAM
                    && c->fence && (!fence || strcmp (c->fence, fence) != 0)) {
        zhash_delete (ctx->commits, sender);
    }
    if (!(c = zhash_lookup (ctx->commits, sender))) {
        c = commit_create ();
        c->state = COMMIT_STORE;
        c->dirents = dirents;
        dirents = NULL;
        zhash_insert (ctx->commits, sender, c);
        zhash_freefn (ctx->commits, sender, (zhash_free_fn *)commit_destroy);
    } else if (c->state == COMMIT_PUT) {
        c->state = COMMIT_STORE;
        if (!fence)
            monotime (&c->t0);
    }
    if (fence && !c->fence)
        c->fence = xstrdup (fence);

    if (ctx->master && c->state != COMMIT_STORE) { /* XXX */
        flux_log (h, LOG_ERR, "XXX encountered old commit state=%d"
                " fence_name=%s",
                c->state, c->fence ? c->fence : "");
        goto done;
    }

    /* Master: apply the commit and generate a response (subject to rate lim)
     */
    if (ctx->master) {
        FASSERT (h, c->state == COMMIT_STORE);
        if (!(fence && internal)) { /* setting c->request means reply needed */
            FASSERT (h, c->request == NULL);
            if (!(c->request = flux_msg_copy (msg, false)))
                goto error;
        }
        if (fence) {
            c->state = COMMIT_FENCE;
            fence_t *f;
            if (!(f = zhash_lookup (ctx->fences, fence))) {
                f = fence_create (nprocs);
                zhash_insert (ctx->fences, fence, f);
                zhash_freefn (ctx->fences, fence,
                              (zhash_free_fn *)fence_destroy);
            }
            /* Once count is reached, apply all the commits comprising the
             * fence.  We only time this part as otherwise we would be timing
             * synchronization which may have nothing to do with us.
             */
            if (++f->count == f->nprocs) {
                struct timespec t0;
                monotime (&t0);
                commit_apply_fence (ctx, fence);
                tstat_push (&ctx->stats.fence_time,  monotime_since (t0));
                zhash_delete (ctx->fences, fence);
            }
        } else {
            c->state = COMMIT_MASTER;
            double since = monotime_since (ctx->commit_time) * 1E-3;
            if (since < min_commit_window) {
                if (!ctx->commit_timer_armed) {
                    flux_timer_watcher_reset (ctx->commit_timer,
                                              min_commit_window - since, 0.);
                    flux_watcher_start (ctx->commit_timer);
                    ctx->commit_timer_armed = true;
                }
            } else
                commit_apply_all (ctx);
        }

    /* Slave: commit must wait for any referenced hash entries to be stored
     * upstream, then stash request msg in the commit_t and send a new internal
     * commit upstream.
     */
    } else {
        FASSERT (h, c->state == COMMIT_STORE);
        wait = wait_create (h, w, msg, commit_request_cb, arg);
        if (commit_dirty (ctx, c, wait))
            goto done; /* stall */
        wait_destroy (wait, NULL);
        c->state = COMMIT_UPSTREAM;
        send_upstream_commit (ctx, c, sender, fence, nprocs);
        if (!(fence && internal)) {
            FASSERT (h, c->request == NULL);
            if (!(c->request = flux_msg_copy (msg, false)))
                goto error;
        }
    }
done:
    Jput (in);
    Jput (dirents);
    if (sender)
        free (sender);
    return;
error:
    Jput (in);
    Jput (dirents);
    if (sender)
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

/* For wait_version().
 */
static void sync_request_cb (flux_t h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON out = Jnew ();
    int rootseq;
    wait_t *wait = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "rootseq", &rootseq)) {
        errno = EPROTO;
        goto done;
    }
    if (ctx->rootseq < rootseq) {
        if (!(wait = wait_create (h, w, msg, sync_request_cb, arg)))
            goto done;
        wait_addqueue (ctx->watchlist, wait);
        goto done; /* stall */
    }
    Jadd_int (out, "rootseq", ctx->rootseq);
    Jadd_str (out, "rootdir", ctx->rootdir);
    rc = 0;
done:
    if (!wait) {
        if (flux_respond (h, msg, rc < 0 ? errno : 0,
                                  rc < 0 ? NULL : Jtostr (out)) < 0)
            flux_log_error (h, "%s", __FUNCTION__);
    }
    Jput (in);
    Jput (out);
}

static void getroot_request_cb (flux_t h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    if (!(out = kp_rgetroot_enc (ctx->rootseq, ctx->rootdir)))
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (out);
}

static int getroot_rpc (ctx_t *ctx, int *rootseq, href_t rootdir)
{
    flux_rpc_t *rpc;
    const char *json_str;
    JSON out = NULL;
    const char *ref;
    int rc = -1;

    if (!(rpc = flux_rpc (ctx->h, "kvs.getroot", NULL,
                                             FLUX_NODEID_UPSTREAM, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rgetroot_dec (out, rootseq, &ref) < 0)
        goto done;
    if (strlen (ref) > sizeof (href_t) - 1) {
        errno = EPROTO;
        goto done;
    }
    memcpy (rootdir, ref, sizeof (href_t));
    rc = 0;
done:
    Jput (out);
    flux_rpc_destroy (rpc);
    return rc;
}

/* Alter the (rootdir, rootseq) in response to a setroot event.
 */
static void setroot_event_cb (flux_t h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = NULL;
    int rootseq;
    const char *json_str;
    const char *rootdir;
    const char *fence = NULL;
    JSON root = NULL;

    if (flux_event_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        goto done;
    }
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s: flux_event_decode", __FUNCTION__);
        goto done;
    }
    if (kp_tsetroot_dec (out, &rootseq, &rootdir, &root, &fence) < 0) {
        flux_log_error (ctx->h, "%s: kp_tsetroot_dec", __FUNCTION__);
        goto done;
    }
    if (root) {
        href_t ref;
        Jget (root);
        store (ctx, root, ref);
    }
    setroot (ctx, rootdir, rootseq);
    if (fence)
        commit_complete_fence (ctx, fence, rootdir, rootseq);
done:
    Jput (out);
}

static int setroot_event_send (ctx_t *ctx, const char *fence)
{
    JSON in = NULL;
    JSON root = NULL;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (event_includes_rootdir) {
        bool stall = !load (ctx, ctx->rootdir, NULL, &root);
        FASSERT (ctx->h, stall == false);
    }
    if (!(in = kp_tsetroot_enc (ctx->rootseq, ctx->rootdir, root, fence)))
        goto done;
    if (!(msg = flux_event_encode ("kvs.setroot", Jtostr (in))))
        goto done;
    if (flux_send (ctx->h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_msg_destroy (msg);
    return rc;
}

static bool disconnect_cmp (const flux_msg_t *msg, void *arg)
{
    char *sender = arg;
    char *s = NULL;
    bool match = false;

    if (flux_msg_get_route_first (msg, &s) == 0 && !strcmp (s, sender))
        match = true;
    if (s)
        free (s);
    return match;
}

static void disconnect_request_cb (flux_t h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    char *sender = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        return;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    wait_destroy_match (ctx->watchlist, disconnect_cmp, sender);
    zhash_delete (ctx->commits, sender);
    free (sender);
}

static void stats_cache_objects (ctx_t *ctx, tstat_t *ts, int *sp, int *ni, int *nd)
{
    zlist_t *keys;
    hobj_t *hp;
    char *key;
    int size = 0;
    int incomplete = 0;
    int dirty = 0;

    if (!(keys = zhash_keys (ctx->store)))
        oom ();
    while ((key = zlist_pop (keys))) {
        if (!(hp = zhash_lookup (ctx->store, key)) || !hp->o)
            continue;
        tstat_push (ts, hp->size);
        if (hp->state == HOBJ_DIRTY)
            dirty++;
        else if (hp->state == HOBJ_INCOMPLETE)
            incomplete++;
        size += hp->size;
        free (key);
    }
    zlist_destroy (&keys);
    *sp = size;
    *ni = incomplete;
    *nd = dirty;
}

static void stats_get_cb (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = Jnew ();
    tstat_t ts;
    int size, incomplete, dirty;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    memset (&ts, 0, sizeof (ts));
    stats_cache_objects (ctx, &ts, &size, &incomplete, &dirty);
    Jadd_double (o, "obj size total (MiB)", (double)size/1048576);
    util_json_object_add_tstat (o, "obj size (KiB)", &ts, 1E-3);
    Jadd_int (o, "#obj dirty", dirty);
    Jadd_int (o, "#obj incomplete", incomplete);
    Jadd_int (o, "#pending commits", zhash_size (ctx->commits));
    Jadd_int (o, "#pending fences", zhash_size (ctx->fences));
    Jadd_int (o, "#watchers", wait_queue_length (ctx->watchlist));
    util_json_object_add_tstat (o, "gets (sec)", &ctx->stats.get_time, 1E-3);
    util_json_object_add_tstat (o, "puts (sec)", &ctx->stats.put_time, 1E-3);
    util_json_object_add_tstat (o, "commits (sec)",
                                &ctx->stats.commit_time, 1E-3);
    util_json_object_add_tstat (o, "fences after sync (sec)",
                                &ctx->stats.fence_time, 1E-3);
    util_json_object_add_tstat (o, "commits per update",
                                &ctx->stats.commit_merges, 1);
    Jadd_int (o, "#no-op stores", ctx->stats.noop_stores);
    Jadd_int (o, "#faults", ctx->stats.faults);
    Jadd_int (o, "store revision", ctx->rootseq);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (o)) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (o);
}

static void stats_clear_event_cb (flux_t h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;

    memset (&ctx->stats, 0, sizeof (ctx->stats));
}

static void stats_clear_request_cb (flux_t h, flux_msg_handler_t *w,
                                    const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;

    memset (&ctx->stats, 0, sizeof (ctx->stats));

    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

/* Process arguments of the form key=val
 */
static void setargs (ctx_t *ctx, int argc, char **argv)
{
    int i;
    commit_t *c;

    c = commit_create ();

    for (i = 0; i < argc; i++) {
        json_object *o = NULL;
        href_t ref;
        char *key = xstrdup (argv[i]);
        char *val = strchr (key, '=');
        if (val && *val != '\0') {
            *val++ = '\0';
            if (!(o = json_tokener_parse (val)))
                o = json_object_new_string (val);
            if (o) {
               if (store_by_reference (o)) {
                    store (ctx, o, ref);
                    commit_add (c, key, dirent_create ("FILEREF", ref));
                } else {
                    commit_add (c, key, dirent_create ("FILEVAL", o));
                }
            }
        }
        free (key);
    }

    commit_apply_one (ctx, c);
    commit_destroy (c);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.get",        stats_get_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.stats.clear",      stats_clear_request_cb },
    { FLUX_MSGTYPE_EVENT,   "kvs.stats.clear",      stats_clear_event_cb },
    { FLUX_MSGTYPE_EVENT,   "kvs.setroot",          setroot_event_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",          getroot_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",        dropcache_request_cb },
    { FLUX_MSGTYPE_EVENT,   "kvs.dropcache",        dropcache_event_cb },
    { FLUX_MSGTYPE_EVENT,   "hb",                   heartbeat_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect",       disconnect_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.unwatch",          unwatch_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.sync",             sync_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.put",              put_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",              get_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",            watch_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.load",             load_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.store",            store_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.commit",           commit_request_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t h, int argc, char **argv)
{
    ctx_t *ctx = getctx (h);

    if (!ctx) {
        flux_log_error (h, "error creating KVS context");
        return -1;
    }
    if (flux_event_subscribe (h, "hb") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (!ctx->master) {
        if (flux_event_subscribe (h, "kvs.setroot") < 0) {
            flux_log_error (h, "flux_event_subscribe");
            return -1;
        }
    }
    if (flux_event_subscribe (h, "kvs.dropcache") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (flux_event_subscribe (h, "kvs.stats.") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        return -1;
    }
    if (ctx->master) {
        json_object *rootdir = util_json_object_new_object ();
        href_t href;

        store (ctx, rootdir, href);
        setroot (ctx, href, 0);
        setargs (ctx, argc, argv);
        FASSERT (h, argc == 0 || ctx->rootseq > 0);
    } else {
        href_t href;
        int rootseq;
        if (getroot_rpc (ctx, &rootseq, href) < 0) {
            flux_log_error (h, "getroot");
            return -1;
        }
        setroot (ctx, href, rootseq);
    }
    if (flux_msg_handler_addvec (h, handlers, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        return -1;
    }
    flux_msg_handler_delvec (handlers);
    return 0;
}

MOD_NAME ("kvs");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
