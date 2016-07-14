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

#include "src/common/libutil/sha1.h"
#include "src/common/libutil/shastring.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libutil/log.h"

#include "waitqueue.h"
#include "proto.h"
#include "cache.h"
#include "json_dirent.h"

typedef char href_t[SHA1_STRING_SIZE];

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

/* Expire cache_entry after 'max_lastuse_age' heartbeats.
 */
const int max_lastuse_age = 5;

/* Coalesce commits that arrive within min_commit_window seconds
 * of previous update.
 */
const double min_commit_window = 1E-3;

/* Include root directory in kvs.setroot event.
 */
const bool event_includes_rootdir = true;

typedef struct {
    json_object *ops;       /* array of { "key":fqkey, "dirent":dirent } */
    flux_msg_t *request;    /* request message */
    struct timespec t0;     /* commit begin timestamp */
    char *fence;            /* fence name, if applicable */
    enum {
        COMMIT_NEW,
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
    struct cache *cache;    /* SHA1 => cache_entry */
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

static void commit_timeout_handler (flux_reactor_t *r, flux_watcher_t *w,
                                    int revents, void *arg);

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    if (ctx) {
        cache_destroy (ctx->cache);
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
        ctx->cache = cache_create ();
        ctx->commits = zhash_new ();
        ctx->fences = zhash_new ();
        ctx->watchlist = wait_queue_create ();
        if (!ctx->cache || !ctx->commits || !ctx->fences || !ctx->watchlist) {
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
        if (!(ctx->tok = json_tokener_new ())) {
            errno = ENOMEM;
            goto error;
        }
        flux_aux_set (h, "kvssrv", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static commit_t *commit_create (void)
{
    commit_t *c = xzmalloc (sizeof (*c));
    return c;
}

static void commit_destroy (commit_t *c)
{
    if (c) {
        if (c->ops)
            json_object_put (c->ops);
        if (c->fence)
            free (c->fence);
        flux_msg_destroy (c->request);
        free (c);
    }
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

static void content_load_completion (flux_rpc_t *rpc, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o;
    const void *data;
    int size;
    const char *blobref;
    struct cache_entry *hp;

    if (flux_rpc_get_raw (rpc, NULL, &data, &size) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    blobref = flux_rpc_aux_get (rpc);
    if (!(o = json_tokener_parse_ex (ctx->tok, (char *)data, size))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        json_tokener_reset (ctx->tok);
        goto done;
    }
    if ((hp = cache_lookup (ctx->cache, blobref, ctx->epoch)))
        cache_entry_set_json (hp, o);
    else {
        hp = cache_entry_create (o);
        cache_insert (ctx->cache, blobref, hp);
    }
done:
    flux_rpc_destroy (rpc);
}

/* If now is true, perform the load rpc synchronously;
 * otherwise arrange for a continuation to handle the response.
 */
static int content_load_request_send (ctx_t *ctx, const href_t ref, bool now)
{
    flux_rpc_t *rpc = NULL;

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    if (!(rpc = flux_rpc_raw (ctx->h, "content.load",
                    ref, SHA1_STRING_SIZE, FLUX_NODEID_ANY, 0)))
        goto error;
    flux_rpc_aux_set (rpc, xstrdup (ref), free);
    if (now) {
        content_load_completion (rpc, ctx);
        flux_rpc_destroy (rpc);
    } else if (flux_rpc_then (rpc, content_load_completion, ctx) < 0)
        goto error;
    return 0;
error:
    flux_rpc_destroy (rpc);
    return -1;
}

static bool load (ctx_t *ctx, const href_t ref, wait_t *wait, json_object **op)
{
    struct cache_entry *hp = cache_lookup (ctx->cache, ref, ctx->epoch);
    bool done = true;

    /* Create an incomplete hash entry if none found.
     */
    if (!hp) {
        hp = cache_entry_create (NULL);
        cache_insert (ctx->cache, ref, hp);
        if (content_load_request_send (ctx, ref, wait ? false : true) < 0)
            flux_log_error (ctx->h, "content_load_request_send");
        ctx->stats.faults++;
    }
    /* If hash entry is incomplete (either created above or earlier),
     * arrange to stall caller if wait_t was provided.
     */
    if (!cache_entry_get_valid (hp)) {
        cache_entry_wait (hp, wait);
        done = false; /* stall */
    }

    if (done && op)
        *op = cache_entry_get_json (hp);
    return done;
}

static void content_store_completion (flux_rpc_t *rpc, void *arg)
{
    ctx_t *ctx = arg;
    struct cache_entry *hp;
    const char *blobref;
    int blobref_size;

    if (flux_rpc_get_raw (rpc, NULL, &blobref, &blobref_size) < 0) {
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    if (!blobref || blobref[blobref_size - 1] != '\0') {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s", __FUNCTION__);
        goto done;
    }
    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    hp = cache_lookup (ctx->cache, blobref, ctx->epoch);
    cache_entry_set_dirty (hp, false);
done:
    flux_rpc_destroy (rpc);
}

static int content_store_request_send (ctx_t *ctx, const href_t ref,
                                   json_object *val)
{
    flux_rpc_t *rpc;
    const char *data = Jtostr (val);
    int size = strlen (data) + 1;

    //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, ref);
    if (!(rpc = flux_rpc_raw (ctx->h, "content.store",
                              data, size, FLUX_NODEID_ANY, 0)))
        goto error;
    if (flux_rpc_then (rpc, content_store_completion, ctx) < 0)
        goto error;
    return 0;
error:
    flux_rpc_destroy (rpc);
    return -1;
}

static void store (ctx_t *ctx, json_object *o, href_t ref)
{
    struct cache_entry *hp;
    SHA1_CTX sha1_ctx;
    const char *s = json_object_to_json_string (o);
    uint8_t hash[SHA1_DIGEST_SIZE];

    SHA1_Init (&sha1_ctx);
    SHA1_Update (&sha1_ctx, (uint8_t *)s, strlen (s) + 1);
    SHA1_Final (&sha1_ctx, hash);
    sha1_hashtostr (hash, ref);

    if ((hp = cache_lookup (ctx->cache, ref, ctx->epoch))) {
        if (cache_entry_get_valid (hp)) {
            ctx->stats.noop_stores++;
            json_object_put (o);
        } else
            cache_entry_set_json (hp, o);
    } else {
        hp = cache_entry_create (o);
        cache_insert (ctx->cache, ref, hp);
        cache_entry_set_dirty (hp, true);
        if (content_store_request_send (ctx, ref, o) < 0)
            flux_log_error (ctx->h, "content_store");
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

    json_object_object_foreachC (dir, iter) {
        if (json_object_object_get_ex (iter.val, "DIRVAL", &subdir)) {
            commit_unroll (ctx, subdir); /* depth first */
            json_object_get (subdir);
            store (ctx, subdir, ref);
            json_object_object_add (dir, iter.key,
                                    dirent_create ("DIRREF", ref));
        }
    }
}

/* link (key, dirent) into directory 'dir'.
 */
static void commit_link_dirent (ctx_t *ctx, json_object *rootdir,
                                const char *key, json_object *dirent)
{
    char *cpy = xstrdup (key);
    char *next, *name = cpy;
    json_object *dir = rootdir;
    json_object *o, *subdir = NULL, *subdirent;

    /* This is the first part of a key with multiple path components.
     * Make sure that it is a directory in DIRVAL form, then recurse
     * on the remaining path components.
     */
    while ((next = strchr (name, '.'))) {
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
            subdir = copydir (subdir);/* do not corrupt store by modify orig. */
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else if (json_object_object_get_ex (subdirent, "LINKVAL", &o)) {
            FASSERT (ctx->h, json_object_get_type (o) == json_type_string);
            char *nkey = xasprintf ("%s.%s", json_object_get_string (o), next);
            commit_link_dirent (ctx, rootdir, nkey, dirent);
            free (nkey);
            goto done;
        } else {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto done;
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        }
        name = next;
        dir = subdir;
    }
    /* This is the final path component of the key.  Add it to the directory.
     */
    if (dirent)
        json_object_object_add (dir, name, json_object_get (dirent));
    else
        json_object_object_del (dir, name);
done:
    free (cpy);
}

static json_object *commit_apply_start (ctx_t *ctx)
{
    json_object *rootdir = NULL;
    bool stall = !load (ctx, ctx->rootdir, NULL, &rootdir);

    FASSERT (ctx->h, !stall);
    FASSERT (ctx->h, rootdir != NULL);
    return copydir (rootdir); /* do not corrupt store by modifying orig. */
}

static void commit_apply_ops (ctx_t *ctx, json_object *rootcpy, commit_t *c)
{
    int i, len;
    json_object *op, *dirent;
    const char *key;

    if (c->ops) {
        len = json_object_array_length (c->ops);
        for (i = 0; i < len; i++) {
            if (!(op = json_object_array_get_idx (c->ops, i))
                    || !Jget_str (op, "key", &key))
                continue;
            dirent = NULL;
            (void)Jget_obj (op, "dirent", &dirent); /* NULL for unlink */
            commit_link_dirent (ctx, rootcpy, key, dirent);
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
            commit_apply_ops (ctx, rootcpy, c);
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
            commit_apply_ops (ctx, rootcpy, c);
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
    if (count > 1)
        flux_log (ctx->h, LOG_DEBUG, "coalesced %d commits", count);
}

static void dropcache_request_cb (flux_t h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    int size, expcount = 0;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto done;
    size = cache_count_entries (ctx->cache);
    expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0);
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

    if (flux_event_decode (msg, NULL, NULL) < 0)
        return;
    size = cache_count_entries (ctx->cache);
    expcount = cache_expire_entries (ctx->cache, ctx->epoch, 0);
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

    cache_expire_entries (ctx->cache, ctx->epoch, max_lastuse_age);
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
        if (Jget_str (dirent, "LINKVAL", &link)) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (ctx, root, link, &dirent, wait, false, depth))
                goto stall;
            if (!dirent)
                goto error;
        }
        if (Jget_str (dirent, "DIRREF", &ref)) {
            if (!load (ctx, ref, wait, &dir))
                goto stall;

        } else if (json_object_object_get_ex (dirent, "DIRVAL", &dir)) {
            /* N.B. in current code, directories are never stored by value */
            log_msg_exit ("%s: unexpected DIRVAL: path=%s name=%s: dirent=%s ",
                      __FUNCTION__, path, name, Jtostr (dirent));
        } else if ((Jget_str (dirent, "FILEREF", NULL)
                 || json_object_object_get_ex (dirent, "FILEVAL", NULL))) {
            errno = ENOTDIR;
            goto error;
        } else {
            log_msg_exit ("%s: unknown dirent type: path=%s name=%s: dirent=%s ",
                      __FUNCTION__, path, name, Jtostr (dirent));
        }
        name = next;
    }
    /* now terminal path component */
    if (json_object_object_get_ex (dir, name, &dirent) &&
        Jget_str (dirent, "LINKVAL", &link)) {
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
        if (Jget_str (dirent, "DIRREF", &ref)) {
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
        } else if (Jget_str (dirent, "FILEREF", &ref)) {
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
            log_msg_exit ("%s: corrupt internal storage", __FUNCTION__);
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

static bool compare_json (json_object *o1, json_object *o2)
{
    const char *s1 = json_object_to_json_string (o1);
    const char *s2 = json_object_to_json_string (o2);

    return !strcmp (s1, s2);
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
    if (first || !compare_json (val, oval) != 0) {
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
    if (cache_wait_destroy_match (ctx->cache, unwatch_cmp, &p) < 0)
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    Jput (in);
    if (p.sender)
        free (p.sender);
}

static void commit_response_cb (flux_t h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    const char *rootdir, *sender;
    int rootseq;
    commit_t *commit;

    if (flux_response_decode (msg, NULL, &json_str) < 0)
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
done:
    Jput (in);
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

    if (!(in = kp_tcommit_enc (sender, c->ops, fence, nprocs)))
        goto error;
    if (!(rpc = flux_rpc (ctx->h, "kvs.commit", Jtostr (in),
                          FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
        goto error;
    flux_rpc_destroy (rpc);
    Jput (c->ops);
    c->ops = NULL;
    Jput (in);
    return 0;
error:
    flux_log_error (ctx->h, "%s", __FUNCTION__);
    Jput (in);
    return -1;
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
    if (flux_respond (ctx->h, msg, rc < 0 ? errno : 0,
                                   rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (ctx->h, "%s", __FUNCTION__);
    Jput (out);
}

static void commit_slave (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON ops = NULL;
    commit_t *c = NULL;
    char *sender = NULL;
    const char *arg_sender;
    int nprocs;
    const char *fence = NULL;
    bool internal = false;
    int saved_errno;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (kp_tcommit_dec (in, &arg_sender, &ops, &fence, &nprocs) < 0)
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
        c->state = COMMIT_NEW;
        c->ops = ops;
        ops = NULL;
        zhash_insert (ctx->commits, sender, c);
        zhash_freefn (ctx->commits, sender, (zhash_free_fn *)commit_destroy);
    }
    if (fence && !c->fence)
        c->fence = xstrdup (fence);

    FASSERT (h, c->state == COMMIT_NEW);
    c->state = COMMIT_UPSTREAM;
    send_upstream_commit (ctx, c, sender, fence, nprocs);
    if (!(fence && internal)) {
        FASSERT (h, c->request == NULL);
        if (!(c->request = flux_msg_copy (msg, false)))
            goto error;
    }
    Jput (in);
    Jput (ops);
    if (sender)
        free (sender);
    return;
error:
    saved_errno = errno;
    Jput (in);
    Jput (ops);
    if (sender)
        free (sender);
    if (flux_respond (h, msg, saved_errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void commit_master (flux_t h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    JSON in = NULL;
    JSON ops = NULL;
    commit_t *c = NULL;
    char *sender = NULL;
    const char *arg_sender;
    int nprocs;
    const char *fence = NULL;
    bool internal = false;
    int saved_errno;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (kp_tcommit_dec (in, &arg_sender, &ops, &fence, &nprocs) < 0)
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
        c->state = COMMIT_NEW;
        c->ops = ops;
        ops = NULL;
        zhash_insert (ctx->commits, sender, c);
        zhash_freefn (ctx->commits, sender, (zhash_free_fn *)commit_destroy);
    }
    if (fence && !c->fence)
        c->fence = xstrdup (fence);

    if (c->state != COMMIT_NEW) { /* XXX */
        flux_log (h, LOG_ERR, "XXX master encountered old commit state=%d"
                " fence_name=%s",
                c->state, c->fence ? c->fence : "");
        goto done;
    }

    FASSERT (h, c->state == COMMIT_NEW);
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

done:
    Jput (in);
    Jput (ops);
    if (sender)
        free (sender);
    return;
error:
    saved_errno = errno;
    Jput (in);
    Jput (ops);
    if (sender)
        free (sender);
    if (flux_respond (h, msg, saved_errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void commit_request_cb (flux_t h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    if (ctx->master)
        commit_master (h, w, msg, arg);
    else
        commit_slave (h, w, msg, arg);
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
    (void)wait_destroy_match (ctx->watchlist, disconnect_cmp, sender);
    (void)cache_wait_destroy_match (ctx->cache, disconnect_cmp, sender);
    zhash_delete (ctx->commits, sender);
    free (sender);
}

static void add_tstat (json_object *o, const char *name, tstat_t *ts,
                       double scale)
{
    json_object_object_add (o, name, tstat_json (ts, scale));
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
    cache_get_stats (ctx->cache, &ts, &size, &incomplete, &dirty);
    Jadd_double (o, "obj size total (MiB)", (double)size/1048576);
    add_tstat (o, "obj size (KiB)", &ts, 1E-3);
    Jadd_int (o, "#obj dirty", dirty);
    Jadd_int (o, "#obj incomplete", incomplete);
    Jadd_int (o, "#pending commits", zhash_size (ctx->commits));
    Jadd_int (o, "#pending fences", zhash_size (ctx->fences));
    Jadd_int (o, "#watchers", wait_queue_length (ctx->watchlist));
    add_tstat (o, "gets (sec)", &ctx->stats.get_time, 1E-3);
    add_tstat (o, "puts (sec)", &ctx->stats.put_time, 1E-3);
    add_tstat (o, "commits (sec)", &ctx->stats.commit_time, 1E-3);
    add_tstat (o, "fences after sync (sec)", &ctx->stats.fence_time, 1E-3);
    add_tstat (o, "commits per update",
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
    { FLUX_MSGTYPE_REQUEST, "kvs.get",              get_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",            watch_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.commit",           commit_request_cb },
    { FLUX_MSGTYPE_RESPONSE, "kvs.commit",          commit_response_cb },
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
        json_object *rootdir = Jnew ();
        href_t href;

        store (ctx, rootdir, href);
        setroot (ctx, href, 0);
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
