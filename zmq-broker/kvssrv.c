/* kvssrv.c - distributed key-value store based on hash tree */

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

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <sys/time.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>
#include <fnmatch.h>

#include "zmsg.h"
#include "plugin.h"
#include "util.h"
#include "tstat.h"
#include "log.h"
#include "waitqueue.h"

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

/* Hash object in ctx->store by SHA1 key.
 */
typedef struct {
    waitqueue_t waitlist;   /* waiters for HOBJ_COMPLETE */
    json_object *o;         /* value object */
    int size;               /* size of value for stats, est by serialization */
    int lastuse_epoch;      /* time of last use for cache expiry */
    enum {
        HOBJ_COMPLETE,      /* nothing in progress, o != NULL */
        HOBJ_INCOMPLETE,    /* load in progress, o == NULL */
        HOBJ_DIRTY,         /* store in progress, o != NULL */
    } state;
} hobj_t;

/* A commit_t is created to contain "put" metadata until kvs_commit is called.
 */
typedef struct {
    zhash_t *dirents;       /* hash key (e.g. a.b.c) => dirent */
    zmsg_t *request;        /* request message */
    bool closed;            /* commit cannot be ammended, clock started */
    struct timespec t0;     /* commit begin timestamp */
    bool internal;          /* commit generated internally, don't time this */
} commit_t;

/* A get_t is created to time kvs_get.
 * Only needed for tracking the total time servicing a get including stalls.
 */
typedef struct {
    struct timespec t0;
} get_t;

/* Statistics gathered for kvs.stats.get, etc.
 */
typedef struct {
    tstat_t commit_time;
    tstat_t get_time;
    tstat_t put_time;
    int noop_stores;
} stats_t;

typedef struct {
    zhash_t *store;         /* hash SHA1 => hobj_t */
    href_t rootdir;         /* current root SHA1 */
    int rootseq;            /* current root version (for ordering) */
    zhash_t *commits;       /* hash of pending put/commits by sender name */
    zhash_t *gets;          /* hash of pending get requests by sender name */
    waitqueue_t watchlist;
    int watchlist_lastrun_epoch;
    stats_t stats;
    flux_t h;
    bool master;            /* for now minimize flux_treeroot() calls */
    int epoch;              /* tracks current event.sched.trigger epoch */
} ctx_t;

enum {
    KVS_GET_DIRVAL = 1,
    KVS_GET_FILEVAL = 2,
};

static int setroot_event_send (ctx_t *ctx);

static void freectx (ctx_t *ctx)
{
    if (ctx->store)
        zhash_destroy (&ctx->store);
    if (ctx->commits)
        zhash_destroy (&ctx->commits);
    if (ctx->gets)
        zhash_destroy (&ctx->gets);
    if (ctx->watchlist)
        wait_queue_destroy (ctx->watchlist);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "kvssrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->store = zhash_new ()))
            oom ();
        if (!(ctx->commits = zhash_new ()))
            oom ();
        if (!(ctx->gets = zhash_new ()))
            oom ();
        ctx->watchlist = wait_queue_create ();
        ctx->h = h;
        ctx->master = flux_treeroot (h);
        flux_aux_set (h, "kvssrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
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
    if (!(c->dirents = zhash_new ()))
        oom ();
    return c;
}

static void commit_destroy (commit_t *c)
{
    zhash_destroy (&c->dirents);
    free (c);
}

static void commit_add (commit_t *c, const char *key, json_object *dirent)
{
    zhash_insert (c->dirents, key, dirent);
    zhash_freefn (c->dirents, key, (zhash_free_fn *)json_object_put);
}

static get_t *get_create (void)
{
    get_t *g = xzmalloc (sizeof (*g));
    monotime (&g->t0);
    return g;
}

static void get_destroy (get_t *g)
{
    free (g);
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

static void load_request_send (ctx_t *ctx, const href_t ref)
{
    json_object *o = util_json_object_new_object ();

    json_object_object_add (o, ref, NULL);
    flux_request_send (ctx->h, o, "kvs.load");
    json_object_put (o);
}

static bool load (ctx_t *ctx, const href_t ref, wait_t w, json_object **op)
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
    } else {
        /* Create an incomplete hash entry if none found.
         */
        if (!hp) {
            hp = hobj_create (ctx, NULL);
            zhash_insert (ctx->store, ref, hp);
            zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
            hp->state = HOBJ_INCOMPLETE;
            load_request_send (ctx, ref);
        }
        /* If hash entry is incomplete (either created above or earlier),
         * arrange to stall caller if wait_t was provided.
         */
        if (hp->state == HOBJ_INCOMPLETE) {
            if (w)
                wait_addqueue (hp->waitlist, w);
            done = false; /* stall */
        }
    }
    if (done) {
        assert (hp != NULL);
        assert (hp->o != NULL);
        if (op)
            *op = hp->o;
    }
    return done;
}

/* Store the results of a load request.
 */
static void load_complete (ctx_t *ctx, href_t ref, json_object *o)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);

    assert (hp != NULL);
    assert (hp->state == HOBJ_INCOMPLETE);
    (void)hobj_update (ctx, hp, o);
    hp->state = HOBJ_COMPLETE;
    wait_runqueue (hp->waitlist);
}

static void store_request_send (ctx_t *ctx, const href_t ref, json_object *val)
{
    json_object *o = util_json_object_new_object ();

    json_object_get (val);
    json_object_object_add (o, ref, val);
    flux_request_send (ctx->h, o, "kvs.store");
    json_object_put (o);
}

/* N.B. cache may have expired (if not dirty) so consider object
 * states of missing and incomplete to be "not dirty" in this context.
 */
static bool store_isdirty (ctx_t *ctx, const href_t ref, wait_t w)
{
    hobj_t *hp = zhash_lookup (ctx->store, ref);

    if (hp && hp->state == HOBJ_DIRTY) {
        if (w)
            wait_addqueue (hp->waitlist, w);
        return true;
    }
    return false;
}

static void store (ctx_t *ctx, json_object *o, href_t ref)
{
    hobj_t *hp; 

    compute_json_href (o, ref);

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

    assert (hp != NULL);
    assert (hp->state == HOBJ_DIRTY);
    hp->state = HOBJ_COMPLETE;
    wait_runqueue (hp->waitlist);
}

static bool readahead_dir (ctx_t *ctx, json_object *dir, wait_t w, int flags)
{
    json_object *val;
    json_object_iter iter;
    const char *ref;
    bool done = true;

    json_object_object_foreachC (dir, iter) {
        if ((flags & KVS_GET_FILEVAL) && util_json_object_get_string (iter.val, "FILEREF", &ref) == 0) {
            if (!load (ctx, ref, w, &val)) {
                done = false;
                continue;
            }
        } else if ((flags & KVS_GET_DIRVAL) && util_json_object_get_string (iter.val, "DIRREF", &ref) == 0){
            if (!load (ctx, ref, w, &val)) {
                done = false;
                continue;
            }
            if (!readahead_dir (ctx, val, w, flags))
                done = false;
        }
    }
    return done;
}

/* Create a JSON object that is a duplicate of the directory 'dir' with
 * references to the content-hash replaced with their values.  More precisely,
 * if 'flags' contains KVS_GET_FILEVAL, replace FILEREF's with their values;
 * if 'flags' contains KVS_GET_DIRVAL, replace DIRREF's with their values,
 * and recurse.
 */
static json_object *kvs_save (ctx_t *ctx, json_object *dir, wait_t w, int flags)
{
    json_object *dcpy, *val;
    json_object_iter iter;
    const char *ref;

    if (!(dcpy = json_object_new_object ()))
        oom ();
    
    json_object_object_foreachC (dir, iter) {
        if ((flags & KVS_GET_FILEVAL)
                && util_json_object_get_string (iter.val, "FILEREF", &ref)==0) {
            if (!load (ctx, ref, w, &val))
                goto stall;
            json_object_object_add (dcpy, iter.key,
                                    dirent_create ("FILEVAL", val));
        } else if ((flags & KVS_GET_DIRVAL)
                && util_json_object_get_string (iter.val, "DIRREF", &ref)==0) {
            if (!load (ctx, ref, w, &val))
                goto stall;
            if (!(val = kvs_save (ctx, val, w, flags)))
                goto stall;
            json_object_object_add (dcpy, iter.key,
                                    dirent_create ("DIRVAL", val));
            json_object_put (val);
        } else {
            json_object_get (iter.val);
            json_object_object_add (dcpy, iter.key, iter.val);
        }
    }
    return dcpy;
stall:
    json_object_put (dcpy);
    return NULL;
}

/* Given a JSON object created by kvs_save(), restore it to the content-hash
 * and return the new reference in 'href'.
 */
static void kvs_restore (ctx_t *ctx, json_object *dir, href_t href)
{
    json_object *cpy, *o;
    json_object_iter iter;
    href_t nhref;

    if (!(cpy = json_object_new_object ()))
        oom ();

    json_object_object_foreachC (dir, iter) {
        if ((o = json_object_object_get (iter.val, "DIRVAL"))) {
            kvs_restore (ctx, o, nhref);
            json_object_object_add (cpy, iter.key,
                                    dirent_create ("DIRREF", nhref));
        } else if ((o = json_object_object_get (iter.val, "FILEVAL"))
                                        && store_by_reference (o)) {
            json_object_get (o);
            store (ctx, o, nhref);
            json_object_object_add (cpy, iter.key,
                                        dirent_create ("FILEREF", nhref));
        } else { /* FILEVAL, FILEREF, DIRREF */
            json_object_get (iter.val);
            json_object_object_add (cpy, iter.key, iter.val);
        }
    }
    store (ctx, cpy, href);
}

static bool decode_rootref (const char *rootref, int *seqp, href_t ref)
{
    char *p;
    int seq = strtoul (rootref, &p, 10);

    if (*p++ != '.' || strlen (p) + 1 != sizeof (href_t))
        return false;
    *seqp = seq;
    memcpy (ref, p, sizeof (href_t));
    return true;
}

static char *encode_rootref (int seq, href_t ref)
{
    char *rootref;

    if (asprintf (&rootref, "%d.%s", seq, ref) < 0)
        oom ();
    return rootref;
}

static void setroot (ctx_t *ctx, int seq, href_t ref)
{
    if (seq == 0 || seq > ctx->rootseq) {
        memcpy (ctx->rootdir, ref, sizeof (href_t));
        ctx->rootseq = seq;
        wait_runqueue (ctx->watchlist);
        ctx->watchlist_lastrun_epoch = ctx->epoch;
    }
}

/* Put (name,ndirent) to deep copy of root directory. 
 */
static void deep_put (json_object *dir, char *nkey, json_object *ndirent)
{
    char *next, *name = nkey;
    json_object *dirent;

    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if ((dirent = json_object_object_get (dir, name))) {
            if (!json_object_object_get (dirent, "DIRVAL")) {
                json_object_object_del (dir, name);
                dirent = NULL;
            }
        }
        if (!dirent) {
            dirent = dirent_create ("DIRVAL", NULL); /* empty dir */
            json_object_object_add (dir, name, dirent);
        }
        dir = json_object_object_get (dirent, "DIRVAL");
        name = next;
    }
    /* dir now is the directory that contains the final path component */
    json_object_object_del (dir, name);
    if (ndirent) {
        json_object_get (ndirent);
        json_object_object_add (dir, name, ndirent); /* consumes ndirent */
    }
}

/* This is the rather heavy-weight and naive code that applies commit_t 'c'
 * to the store.  It should only be called on the master.
 */
void commit_apply (ctx_t *ctx, commit_t *c)
{
    json_object *dir, *cpy;
    href_t ref;
    json_object *dirent;
    zlist_t *keys;
    char *key;
    json_object *vers;

    /* Load the entire store into 'cpy'.
     */
    (void)load (ctx, ctx->rootdir, NULL, &dir);
    assert (dir != NULL);
    cpy = kvs_save (ctx, dir, NULL, KVS_GET_DIRVAL);
    assert (cpy != NULL);

    /* Apply the key=>val updates in the commit to 'cpy'.
     */
    keys = zhash_keys (c->dirents);
    while ((key = zlist_pop (keys))) {
        dirent = zhash_lookup (c->dirents, key);
        deep_put (cpy, key, dirent);
        free (key);
    }
    zlist_destroy (&keys);

    /* Store version=version + 1 in 'cpy'.
     */
    if (!(vers = json_object_new_int (ctx->rootseq + 1)))
        oom ();
    deep_put (cpy, "version", dirent_create ("FILEVAL", vers));
    json_object_put (vers);

    /* Write 'cpy' to the store and move hte root pointer to the new 'ref'.
     */
    kvs_restore (ctx, cpy, ref);
    json_object_put (cpy);
    setroot (ctx, ctx->rootseq + 1, ref);
}

static void putdir_int (json_object *dir, const char *name, int i)
{
    json_object *o;
    if (!(o = json_object_new_int (i)))
        oom ();
    json_object_object_del (dir, name);
    json_object_object_add (dir, name, dirent_create ("FILEVAL", o));
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

    //msg ("XXX %s (%s)", __FUNCTION__, dir
    // ? json_object_to_json_string_ext (dir, JSON_C_TO_STRING_PLAIN) : "NULL");

    if (!(new = zhash_new ()))
        oom ();
    json_object_object_foreachC (dir, iter) {
        if ((subdir = json_object_object_get (iter.val, "DIRVAL"))) {
            commit_unroll (ctx, subdir); /* depth first */
            json_object_get (subdir);
            store (ctx, subdir, ref);
            //msg ("XXX %s (%s, %s) => %s", __FUNCTION__, iter.key, ref,
            // json_object_to_json_string_ext (subdir, JSON_C_TO_STRING_PLAIN));
            zhash_insert (new, iter.key, xstrdup (ref));
        }
    }
    /* hash contains name => SHA1 ref */
    if (!(keys = zhash_keys (new)))
        oom ();
    while ((key = zlist_pop (keys))) {
        refcpy = zhash_lookup (new, key);
        assert (refcpy != NULL);
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

    //msg ("XXX %s (%s, %s)", __FUNCTION__, key, dirent
    //      ? json_object_to_json_string_ext (dirent, JSON_C_TO_STRING_PLAIN)
    //      : "NULL");

    /* This is the first part of a key with multiple path components.
     * Make sure that it is a directory in DIRVAL form, then recurse
     * on the remaining path components.
     */
    if ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!(subdirent = json_object_object_get (dir, name))) {
            if (!dirent) /* key deletion - it doesn't exist so return */
                goto done;
            if (!(subdir = json_object_new_object ()))
                oom ();
            json_object_object_add (dir, name, dirent_create ("DIRVAL",subdir));
            json_object_put (subdir);
        } else if ((o = json_object_object_get (subdirent, "DIRVAL"))) {
            subdir = o;
        } else if ((o = json_object_object_get (subdirent, "DIRREF"))) {
            bool stall = !load (ctx, json_object_get_string (o), NULL, &subdir);
            assert (stall == false);
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

void commit_apply_lite (ctx_t *ctx, commit_t *c)
{
    json_object *rootdir = NULL;
    json_object *rootcpy;
    zlist_t *keys;
    char *key;
    json_object *dirent;
    href_t ref;

    /* Load the current root directory into 'rootdir'.
     */
    bool stall = !load (ctx, ctx->rootdir, NULL, &rootdir);
    assert (!stall);
    assert (rootdir != NULL);
    rootcpy = copydir (rootdir); /* do not corrupt store by modifying orig. */

    /* Iterate through the (key, dirent) pairs in the commit_t applying
     * them to rootcpy.  Any changed directories are converted to DIRVALs
     * (non-recursively) to be written out after all the commit_t changes
     * are applied.
     */
    if (!(keys = zhash_keys (c->dirents)))
        oom ();
    while ((key = zlist_pop (keys))) {
        dirent = zhash_lookup (c->dirents, key);
        commit_link_dirent (ctx, rootcpy, key, dirent);
        free (key);
    }
    zlist_destroy (&keys);
    /* Now that we're done applying commit_t changes, convert DIRVALs back
     * to DIRREFs.
     */
    commit_unroll (ctx, rootcpy);

    /* 'rootcpy' is now the new root directory.  Write it to the store
     * and update the root href.
     */
    putdir_int (rootcpy, "version", ctx->rootseq + 1); /* one last change */
    store (ctx, rootcpy, ref);
    //msg ("XXX %s %s => %s", __FUNCTION__, ref,
    //     json_object_to_json_string_ext (rootcpy, JSON_C_TO_STRING_PLAIN));
    setroot (ctx, ctx->rootseq + 1, ref);
}

static int load_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *val, *cpy = NULL, *o = NULL;
    json_object_iter iter;
    wait_t w = NULL;
    bool stall = false;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    w = wait_create (h, typemask, zmsg, load_request_cb, arg);
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!load (ctx, iter.key, w, &val))
            stall = true;
        else {
            json_object_get (val);
            json_object_object_add (cpy, iter.key, val);
        }
    }
    if (!stall) {
        wait_destroy (w, zmsg);
        flux_respond (ctx->h, zmsg, cpy);
    }
done:
    if (o)
        json_object_put (o);
    if (cpy)
        json_object_put (cpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int load_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    json_object_iter iter;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        load_complete (ctx, iter.key, iter.val); /* href -> value */
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int store_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *cpy = NULL, *o = NULL;
    json_object_iter iter;
    href_t href;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        store (ctx, iter.val, href);
        if (strcmp (href, iter.key) != 0)
            flux_log (ctx->h, LOG_ERR, "%s: bad href %s", __FUNCTION__, iter.key);
        json_object_object_add (cpy, iter.key, NULL);
    }
    flux_respond (ctx->h, zmsg, cpy);
done:
    if (o)
        json_object_put (o);
    if (cpy)
        json_object_put (cpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int store_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    json_object_iter iter;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        store_complete (ctx, iter.key);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
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

static int dropcache_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    int rc = 0;
    int expcount = 0;
    int sz;

    sz = zhash_size (ctx->store);
    if (ctx->master) {
    /* No dropcache allowed on the master.
     * We cannot clean up here without dropping all client caches too
     * because 'stores' for in-cache objects are suppressed and never
     * reach the master.  Also it's too dangerous to depend on multicast
     * to drop master and slaves at once without flushing pending work.
     */
#if 0
        href_t href;
        json_object *cpy, *rootdir;

        (void)load (ctx, ctx->rootdir, NULL, &rootdir);
        assert (rootdir != NULL);
        cpy = kvs_save (ctx, rootdir, NULL, KVS_GET_DIRVAL | KVS_GET_FILEVAL);
        assert (cpy != NULL);
        zhash_destroy (&ctx->store);
        if (!(ctx->store = zhash_new ()))
            oom ();
        kvs_restore (ctx, cpy, href);
        assert (!strcmp (ctx->rootdir, href));
        json_object_put (cpy);
#endif
    } else {
        expcount = expire_cache (ctx, 0);
    }
    flux_log (h, LOG_ALERT, "dropped %d of %d cache entries", expcount, sz);
    if ((typemask & FLUX_MSGTYPE_REQUEST))
        flux_respond_errnum (h, zmsg, rc);
    return 0;
}

static int heartbeat_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *event = NULL;
    int expcount = 0;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
                || util_json_object_get_int (event, "epoch", &ctx->epoch) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (ctx->master) {
        /* no cache expiry on master - see comment in dropcache_cb */
    } else {
        /* "touch" objects involved in watched keys */
        if (ctx->epoch - ctx->watchlist_lastrun_epoch > max_lastuse_age) {
            wait_runqueue (ctx->watchlist);
            ctx->watchlist_lastrun_epoch = ctx->epoch;
        }
        /* "touch" root */
        (void)load (ctx, ctx->rootdir, NULL, NULL);

        expcount = expire_cache (ctx, max_lastuse_age);
    }
    if (expcount > 0)
        flux_log (ctx->h, LOG_INFO, "expired %d cache entries", expcount);
done:
    if (event)
        json_object_put (event);
    return 0;
}

/* Get dirent containing requested key.
 */
static bool walk (ctx_t *ctx, json_object *root, const char *path,
                  json_object **direntp, wait_t w, bool readlink, int depth)
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
        if (!(dirent = json_object_object_get (dir, name)))
            goto error;
        if (util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (ctx, root, link, &dirent, w, false, depth))
                goto stall;
            if (!dirent)
                goto error;
        }
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (!load (ctx, ref, w, &dir))
                goto stall;
        } else
            msg_exit ("%s: corrupt internal storage", __FUNCTION__);
        name = next;
    }
    /* now terminal path component */
    dirent = json_object_object_get (dir, name);
    if (dirent && util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
        if (!readlink) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (ctx, root, link, &dirent, w, readlink, depth))
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

static bool lookup (ctx_t *ctx, json_object *root, wait_t w,
                    bool dir, int dir_flags, bool readlink, const char *name,
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
        //msg ("XXX %s: %s", __FUNCTION__, name);
        if (!walk (ctx, root, name, &dirent, w, readlink, 0))
            goto stall;
        if (!dirent) {
            //errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }
        //msg ("XXX %s: %s: %s", __FUNCTION__, name,
        //    json_object_to_json_string_ext (dirent, JSON_C_TO_STRING_PLAIN));
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (!dir) {
                errnum = EISDIR;
                goto done;
            }
            if (!load (ctx, ref, w, &val))
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
            if (!load (ctx, ref, w, &val))
                goto stall;
        } else if ((vp = json_object_object_get (dirent, "DIRVAL"))) {
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
        } else if ((vp = json_object_object_get (dirent, "FILEVAL"))) {
            if (readlink) {
                errnum = EINVAL;
                goto done;
            }
            if (dir) {
                errnum = ENOTDIR;
                goto done;
            }
            val = vp;
        } else if ((vp = json_object_object_get (dirent, "LINKVAL"))) {
            assert (readlink == true); /* walk() ensures this */
            assert (dir == false); /* dir && readlink should never happen */
            val = vp;
        } else
            msg_exit ("%s: corrupt internal storage", __FUNCTION__);
    }
    /* val now contains the requested object */
    if (isdir) {
        if (!ctx->master && !readahead_dir (ctx, val, w, dir_flags))
            goto stall;
        if (!(val = kvs_save (ctx, val, w, dir_flags)))
            goto stall;
    } else 
        json_object_get (val);
done:
    *valp = val;
    if (errnum != 0)
        *ep = errnum;
    return true;
stall:
    return false;
}

static int get_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *root, *reply = NULL, *o = NULL;
    json_object_iter iter;
    wait_t w = NULL;
    bool stall = false;
    bool flag_directory = false;
    bool flag_readlink = false;
    int errnum = 0;
    char *sender = NULL;
    get_t *g;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!(sender = cmb_msg_sender (*zmsg))) {
        flux_log (h, LOG_ERR, "%s: could not determine message sender",
                  __FUNCTION__);
        goto done;
    }
    if (!(g = zhash_lookup (ctx->gets, sender))) {
        g = get_create ();
        zhash_insert (ctx->gets, sender, g);
        zhash_freefn (ctx->gets, sender, (zhash_free_fn *)get_destroy);
    }
    w = wait_create (h, typemask, zmsg, get_request_cb, arg);
    if (!load (ctx, ctx->rootdir, w, &root)) {
        stall = true;
        goto done;
    }
    /* handle flags - they apply to all keys in the request */
    (void)util_json_object_get_boolean (o, ".flag_directory", &flag_directory);
    (void)util_json_object_get_boolean (o, ".flag_readlink", &flag_readlink);

    reply = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6)) /* ignore flags */
            continue;
        json_object *val = NULL;
        if (!lookup (ctx, root, w, flag_directory, 0, flag_readlink,
                     iter.key, &val, &errnum))
            stall = true; /* keep going to maximize readahead */
        if (stall) {
            if (val)
                json_object_put (val);
            continue;
        }
        json_object_object_add (reply, iter.key, val);
    }
    /* if any key encountered an error, the whole request fails */
    /* N.B. unset values are returned as NULL and are not an error */
    if (errnum != 0) {
        wait_destroy (w, zmsg); /* get back *zmsg */
        flux_respond_errnum (ctx->h, zmsg, errnum);
    } else if (!stall) {
        wait_destroy (w, zmsg); /* get back *zmsg */
        (void)cmb_msg_replace_json (*zmsg, reply);
        flux_response_sendmsg (ctx->h, zmsg);
    }
    tstat_push (&ctx->stats.get_time, monotime_since (g->t0));
    zhash_delete (ctx->gets, sender);
done:
    if (o)
        json_object_put (o);
    if (reply)
        json_object_put (reply);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
    return 0;
}

static int watch_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    char *sender = cmb_msg_sender (*zmsg);
    json_object *root, *reply = NULL, *o = NULL;
    json_object_iter iter;
    wait_t w = NULL;
    bool stall = false, changed = false, reply_sent = false;
    bool flag_directory = false, flag_first = false;
    bool flag_readlink = false, flag_once = false;
    int errnum = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    w = wait_create (h, typemask, zmsg, watch_request_cb, arg);
    if (!load (ctx, ctx->rootdir, w, &root)) {
        stall = true;
        goto done;
    }
    /* handle flags - they apply to all keys in the request */
    (void)util_json_object_get_boolean (o, ".flag_directory", &flag_directory);
    (void)util_json_object_get_boolean (o, ".flag_readlink", &flag_readlink);
    (void)util_json_object_get_boolean (o, ".flag_once", &flag_once);
    (void)util_json_object_get_boolean (o, ".flag_first", &flag_first);

    reply = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6) || !strncmp (iter.key, ".arg_", 5))
            continue;
        json_object *val = NULL;
        if (!lookup (ctx, root, w, flag_directory, 0, flag_readlink,
                     iter.key, &val, &errnum))
            stall = true; /* keep going to maximize readahead */
        if (stall) {
            if (val)
                json_object_put (val);
            continue;
        }
        if (!util_json_match (iter.val, val))
            changed = true;
        json_object_object_add (reply, iter.key, val);
    }
    /* If any key encountered an error, the whole request fails.
     * Unset values are returned as NULL and are not an error.
     * After an error is returned, the key is no longer watched.
     */
    if (errnum != 0) {
        wait_destroy (w, zmsg); /* get back *zmsg */
        flux_respond_errnum (ctx->h, zmsg, errnum);
    } else if (!stall) {
        wait_destroy (w, zmsg); /* get back *zmsg */
        (void)cmb_msg_replace_json (*zmsg, reply);

        /* Reply to the watch request.
         * flag_first is generally true on first call, false thereafter
         */
        if (changed || flag_first) {
            zmsg_t *zcpy;
            if (!(zcpy = zmsg_dup (*zmsg)))
                oom ();
            flux_response_sendmsg (ctx->h, &zcpy);
            reply_sent = true;
        }
   
        /* Resubmit the watch request (clear flag_first) */
        if (!reply_sent || !flag_once) {
            util_json_object_add_boolean (reply, ".flag_directory", flag_directory);
            util_json_object_add_boolean (reply, ".flag_readlink", flag_readlink);
            util_json_object_add_boolean (reply, ".flag_once", flag_once);
            (void)cmb_msg_replace_json (*zmsg, reply);

            /* On every commit, __FUNCTION__ (zcpy) will be called.
             * No reply will be generated unless a value has changed.
             */
            w = wait_create (h, typemask, zmsg, watch_request_cb, arg);
            wait_set_id (w, sender);
            wait_addqueue (ctx->watchlist, w);
        }
    }
done:
    if (o)
        json_object_put (o);
    if (reply)
        json_object_put (reply);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
    return 0;
}

static int put_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    json_object_iter iter;
    href_t ref;
    bool flag_mkdir = false;
    bool flag_symlink = false;
    struct timespec t0;
    char *sender = NULL;
    commit_t *c;

    monotime (&t0);
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!(sender = cmb_msg_sender (*zmsg))) {
        flux_log (h, LOG_ERR, "%s: could not determine message sender",
                  __FUNCTION__);
        goto done;
    }
    if (!(c = zhash_lookup (ctx->commits, sender))) {
        c = commit_create ();
        zhash_insert (ctx->commits, sender, c);
        zhash_freefn (ctx->commits, sender, (zhash_free_fn *)commit_destroy);
    }
    /* Handle commit already in progress.
     */
    if (c->closed) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    (void)util_json_object_get_boolean (o, ".flag_mkdir", &flag_mkdir);
    (void)util_json_object_get_boolean (o, ".flag_symlink", &flag_symlink);
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6)) /* ignore flags */
            continue;
        if (json_object_get_type (iter.val) == json_type_null) {
            if (flag_mkdir) {
                json_object *empty_dir = util_json_object_new_object ();
                store (ctx, empty_dir, ref);
                commit_add (c, iter.key, dirent_create ("DIRREF", ref));
            } else
                commit_add (c, iter.key, NULL);
        } else if (flag_symlink) {
            commit_add (c, iter.key, dirent_create ("LINKVAL", iter.val));
        } else if (store_by_reference (iter.val)) {
            json_object_get (iter.val);
            store (ctx, iter.val, ref);
            commit_add (c, iter.key, dirent_create ("FILEREF", ref));
        } else {
            commit_add (c, iter.key, dirent_create ("FILEVAL", iter.val));
        }
    }
    flux_respond_errnum (h, zmsg, 0); /* success */
    tstat_push (&ctx->stats.put_time, monotime_since (t0));
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
    return 0;
}

/* Send a commit message upstream.
 */
static void send_upstream_commit (ctx_t *ctx, commit_t *c, const char *name,
                                  zmsg_t **zmsg)
{
    json_object *request, *dirent;
    zlist_t *keys;
    char *key;

    /* Stash (take ownership of) orig. commit request.
     */
    assert (c->request == NULL);
    c->request = *zmsg;
    *zmsg = NULL;

    /* Send a new request upstream.
     * When the response is recieved, we will look up this commit_t by name
     * and respond to c->request.
     */
    request = util_json_object_new_object ();
    util_json_object_add_string (request, ".arg_name", name);
    if (!(keys = zhash_keys (c->dirents)))
        oom ();
    while ((key = zlist_pop (keys))) {
        dirent = zhash_lookup (c->dirents, key);
        if (dirent)
            json_object_get (dirent);
        json_object_object_add (request, key, dirent);
        free (key);
    }
    zlist_destroy (&keys);
    flux_request_send (ctx->h, request, "kvs.commit");
}

/* If commit contains references to dirty hash entries (store in progress),
 * arrange for commit_request_cb () to be restarted once these are all clean.
 * Return true if handler should stall; false if there are no dirty references.
 */
static bool commit_dirty (ctx_t *ctx, commit_t *c, wait_t w)
{
    zlist_t *keys;
    char *key;
    json_object *dirent;
    const char *ref;
    bool dirty = false;

    if (!(keys = zhash_keys (c->dirents)))
        oom ();
    while ((key = zlist_pop (keys))) {
        dirent = zhash_lookup (c->dirents, key);
        if (dirent) { 
            if ((util_json_object_get_string (dirent, "FILEREF", &ref) == 0
              || util_json_object_get_string (dirent, "DIRREF", &ref) == 0)
                                            && store_isdirty (ctx, ref, w)) {
                dirty = true;
            }
        }
        free (key);
    }
    zlist_destroy (&keys);

    return dirty;
}

static void commit_respond (ctx_t *ctx, zmsg_t **zmsg, const char *name,
                            const char *rootref)
{
    json_object *response = util_json_object_new_object ();
    
    util_json_object_add_string (response, "name", name);
    if (rootref) {
        util_json_object_add_string (response, "rootref", rootref);
    } else {
        char *tmp = encode_rootref (ctx->rootseq, ctx->rootdir);
        util_json_object_add_string (response, "rootref", tmp);
        free (tmp);
    }
    if (flux_respond (ctx->h, zmsg, response) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: flux_respond: %s", __FUNCTION__,
                  strerror (errno));
    }
    json_object_put (response);
}

static int commit_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    commit_t *c = NULL;
    wait_t w;
    char *sender = NULL;
    json_object_iter iter;
    const char *name;
    bool internal = true;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!(sender = cmb_msg_sender (*zmsg))) {
        flux_log (h, LOG_ERR, "%s: could not determine message sender",
                  __FUNCTION__);
        goto done;
    }
    /* Commits generated internally will contain metadata and .arg_name,
     * while commits generated externally (e.g. kvscli.c) will be empty.
     */
    if (util_json_object_get_string (request, ".arg_name", &name) < 0) {
        name = sender;
        internal = false;
    }
    if (!(c = zhash_lookup (ctx->commits, name))) {
        c = commit_create ();
        c->internal = internal;
        if (c->internal) {
            json_object_object_foreachC (request, iter) {
                if (!strncmp (iter.key, ".arg_", 5))
                    continue;
                if (iter.val)
                    json_object_get (iter.val);
                commit_add (c, iter.key, iter.val);
            }
        }
        zhash_insert (ctx->commits, name, c);
        zhash_freefn (ctx->commits, name, (zhash_free_fn *)commit_destroy);
    }
    /* If this is the first time through, close the commit to new put
     * requests and begin timing the operation.  We only time external
     * commit requests as internal commits are only generated while 
     * servicing external commits and will be included in their times.
     */
    if (!c->closed) {
        c->closed = true;
        if (!c->internal)
            monotime (&c->t0);
    }
    /* Handle commit already in progress (upstream request sent).
     * N.B. we can stall and restart as stores complete but not after req sent.
     */
    if (c->request != NULL) {
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }

    /* Handle zero-length commit.
     */
    if (zhash_size (c->dirents) == 0) {
        commit_respond (ctx, zmsg, name, NULL);
        zhash_delete (ctx->commits, name);

    /* Master: apply the commit and generate a response synchronously.
     */
    } else if (ctx->master) {
#if 0
        commit_apply (ctx, c);      /* updates ctx->rootseq, ctx->rootref */
#else
        commit_apply_lite (ctx, c); /* updates ctx->rootseq, ctx->rootref */
#endif
        setroot_event_send (ctx); 
        commit_respond (ctx, zmsg, name, NULL);
        zhash_delete (ctx->commits, name);

    /* Slave: commit must wait for any referenced hash entries to be stored
     * upstream, then stash 'zmsg' in the commit_t and send a new internal
     * commit upstream.
     */
    } else {
        w = wait_create (h, typemask, zmsg, commit_request_cb, arg);
        if (commit_dirty (ctx, c, w))
            goto done; /* stall */
        wait_destroy (w, zmsg); /* get back *zmsg */
        send_upstream_commit (ctx, c, name, zmsg); 
    }
    if (!c->internal)
        tstat_push (&ctx->stats.commit_time,  monotime_since (c->t0));
done:
    if (request)
        json_object_put (request);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
    return 0;
}

/* We got a response from upstream.
 */
static int commit_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    const char *rootref, *name;
    int seq;
    href_t href;
    commit_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "rootref", &rootref) < 0
            || !decode_rootref (rootref, &seq, href)
            || util_json_object_get_string (o, "name", &name) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    /* Update the cache on this node.
     * If we have already advanced here or beyond, this is a no-op.
     */
    setroot (ctx, seq, href);

    /* Find the original commit_t associated with this request and respond.
     */
    if ((c = zhash_lookup (ctx->commits, name))) {
        assert (c->request != NULL);
        commit_respond (ctx, &c->request, name, rootref);
        if (!c->internal)
            tstat_push (&ctx->stats.commit_time,  monotime_since (c->t0));
        zhash_delete (ctx->commits, name);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int getroot_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    char *rootref = encode_rootref (ctx->rootseq, ctx->rootdir);
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "rootref", rootref);
    flux_respond (h, zmsg, o);
    free (rootref);
    json_object_put (o);
    return 0;
}

static int getroot_request_send (ctx_t *ctx)
{
    int seq;
    href_t href;
    json_object *reply = flux_rpc (ctx->h, NULL, "kvs.getroot");
    const char *rootref;
    int rc = -1;

    if (!reply || util_json_object_get_string (reply, "rootref", &rootref) < 0
               || !decode_rootref (rootref, &seq, href)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad response", __FUNCTION__);
        goto done;
    }
    setroot (ctx, seq, href);
    rc = 0;
done:
    if (reply)
        json_object_put (reply);
    return rc;
}

static int setroot_event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    href_t href;
    int seq;
    const char *rootref;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
                || util_json_object_get_string (o, "rootref", &rootref) < 0
                || !decode_rootref (rootref, &seq, href)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    setroot (ctx, seq, href);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int setroot_event_send (ctx_t *ctx)
{
    char *rootref = encode_rootref (ctx->rootseq, ctx->rootdir);
    json_object *o = util_json_object_new_object ();
    int rc = -1;

    util_json_object_add_string (o, "rootref", rootref);
    if (flux_event_send (ctx->h, o, "event.kvs.setroot") < 0)
        goto done;
    json_object_put (o);
    free (rootref);
    rc = 0;
done:
    return rc;
}

static int disconnect_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                  void *arg)
{
    ctx_t *ctx = arg;
    char *sender = cmb_msg_sender (*zmsg);

    if (sender) {
        wait_destroy_byid (ctx->watchlist, sender);
        zhash_delete (ctx->commits, sender);
        zhash_delete (ctx->gets, sender);
        free (sender);
    }
    zmsg_destroy (zmsg);
    return 0;
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

static int stats_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    char *tag = NULL;
    int rc = -1;

    if (cmb_msg_decode (*zmsg, &tag, &o) < 0) {
        flux_log (h, LOG_ERR, "%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (fnmatch ("*.stats.get", tag, 0) == 0) {
        tstat_t ts;
        int size, incomplete, dirty;

        memset (&ts, 0, sizeof (ts));
        stats_cache_objects (ctx, &ts, &size, &incomplete, &dirty);
        util_json_object_add_double (o, "obj size total (MiB)",
                                     (double)size/1048576);
        util_json_object_add_tstat (o, "obj size (KiB)", &ts, 1E-3);
        util_json_object_add_int (o, "#obj dirty", dirty);
        util_json_object_add_int (o, "#obj incomplete", incomplete);
        util_json_object_add_int (o, "#pending commits",
                                  zhash_size (ctx->commits));
        util_json_object_add_int (o, "#pending gets",
                                  zhash_size (ctx->gets));
        util_json_object_add_int (o, "#watchers",
                                  wait_queue_length (ctx->watchlist));

        util_json_object_add_tstat (o, "commits (sec)",
                                    &ctx->stats.commit_time, 1E-3);

        util_json_object_add_tstat (o, "gets (sec)",
                                    &ctx->stats.get_time, 1E-3);

        util_json_object_add_tstat (o, "puts (sec)",
                                    &ctx->stats.put_time, 1E-3);

        util_json_object_add_int (o, "#no-op stores", ctx->stats.noop_stores);

        util_json_object_add_int (o, "store revision", ctx->rootseq);
 
        if (flux_respond (h, zmsg, o) < 0) {
            err ("%s: flux_respond", __FUNCTION__);
            goto done_stop;
        }
    } else if (fnmatch ("*.stats.clear", tag, 0) == 0) {
        memset (&ctx->stats, 0, sizeof (ctx->stats));
        if ((typemask & FLUX_MSGTYPE_REQUEST)) {
            if (flux_respond_errnum (h, zmsg, 0) < 0) {
                err ("%s: flux_respond_errnum", __FUNCTION__);
                goto done_stop;
            }
        } else
            zmsg_destroy (zmsg);
    }
    /* fall through with zmsg intact on no match */
done:       /* reactor continues */
    rc = 0;
done_stop:  /* reactor terminates */
    if (o)
        json_object_put (o);
    if (tag)
        free (tag);
    return rc;
}

static void setargs (ctx_t *ctx, zhash_t *args)
{
    zlist_t *keys = zhash_keys (args);
    char *key, *val;
    json_object *vo;
    href_t ref;
    commit_t *c;

    c = commit_create ();
    while ((key = zlist_pop (keys))) {
        val = zhash_lookup (args, key);
        if (!(vo = json_tokener_parse (val)))
            vo = json_object_new_string (val);
        if (!vo)
            continue;
        if (store_by_reference (vo)) {
            store (ctx, vo, ref);
            commit_add (c, key, dirent_create ("FILEREF", ref));
        } else {
            commit_add (c, key, dirent_create ("FILEVAL", vo));
        }
        free (key);
    }
    zlist_destroy (&keys);
    commit_apply (ctx, c);
    commit_destroy (c);
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "event.kvs.setroot",    setroot_event_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",          getroot_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.dropcache",        dropcache_cb },
    { FLUX_MSGTYPE_EVENT,   "event.kvs.dropcache",  dropcache_cb },
    { FLUX_MSGTYPE_EVENT,   "event.sched.trigger",  heartbeat_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",              get_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",            watch_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.put",              put_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect",       disconnect_request_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.load",             load_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.load",             load_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.store",            store_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.store",            store_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.commit",           commit_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.commit",           commit_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.stats.*",          stats_cb },
    { FLUX_MSGTYPE_EVENT,   "event.kvs.stats.*",    stats_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int kvssrv_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (flux_event_subscribe (h, "event.sched.trigger") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (!ctx->master) {
        if (flux_event_subscribe (h, "event.kvs.setroot") < 0) {
            flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
            return -1;
        }
    }
    if (flux_event_subscribe (h, "event.kvs.dropcache") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_event_subscribe (h, "event.kvs.stats.") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (ctx->master) {
        json_object *rootdir = util_json_object_new_object ();
        href_t href;

        store (ctx, rootdir, href);
        setroot (ctx, 0, href);
        if (args)
            setargs (ctx, args);
    } else {
        if (getroot_request_send (ctx) < 0)
            return -1;
    }
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

const struct plugin_ops ops = {
    .main    = kvssrv_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
