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

#include "zmsg.h"
#include "plugin.h"
#include "util.h"
#include "log.h"
#include "waitqueue.h"

/* Large values are stored in dirents by reference; small values by value.
 *  (-1 = all by reference, 0 = all by value)
 */
#define LARGE_VAL (sizeof (href_t) + 1)

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

typedef struct {
    waitqueue_t waitlist;
    json_object *o;
    zlist_t *waitlist;
} hobj_t;

/* writeback queue entry */
typedef enum { OP_NAME, OP_STORE, OP_FLUSH } optype_t;
typedef struct {
    optype_t type;
    union {
        struct {
            char *key;
        } name;
        struct {
            char *ref;
        } store;
        struct {
            zmsg_t *zmsg;
        } flush;
    } u;
} op_t;

typedef struct {
    bool done;
    int rootseq;
    href_t rootdir;
    waitqueue_t waitlist;
} commit_t;

typedef struct {
    char *key;
    json_object *dirent;
} name_t;

typedef struct {
    zhash_t *store;
    href_t rootdir;
    int rootseq;
    zhash_t *commits;
    waitqueue_t watchlist;
    struct {
        zlist_t *namequeue;
    } master;
    struct {
        zlist_t *writeback;
        enum { WB_CLEAN, WB_FLUSHING, WB_DIRTY } writeback_state;
    } slave;
    flux_t h;
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
    if (ctx->watchlist)
        wait_queue_destroy (ctx->watchlist);
    if (ctx->slave.writeback)
        zlist_destroy (&ctx->slave.writeback);
    if (ctx->master.namequeue)
        zlist_destroy (&ctx->master.namequeue);
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
        ctx->watchlist = wait_queue_create ();
        if (flux_treeroot (h)) {
            if (!(ctx->master.namequeue = zlist_new ()))
                oom ();
        } else {
            if (!(ctx->slave.writeback = zlist_new ()))
                oom ();
            ctx->slave.writeback_state = WB_CLEAN;
        }
        ctx->h = h;
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
    commit_t *cp = xzmalloc (sizeof (*cp));
    cp->waitlist = wait_queue_create ();
    return cp;
}

static void commit_destroy (commit_t *cp)
{
    wait_queue_destroy (cp->waitlist);
    free (cp);
}

static commit_t *commit_new (ctx_t *ctx, const char *name)
{
    commit_t *cp = commit_create ();

    zhash_insert (ctx->commits, name, cp);
    zhash_freefn (ctx->commits, name, (zhash_free_fn *)commit_destroy);
    return cp;
}

static commit_t *commit_find (ctx_t *ctx, const char *name)
{
    return zhash_lookup (ctx->commits, name);
}

static void commit_done (ctx_t *ctx, commit_t *cp)
{
    memcpy (cp->rootdir, ctx->rootdir, sizeof (href_t));
    cp->rootseq = ctx->rootseq;
    cp->done = true;
}

static op_t *op_create (optype_t type)
{
    op_t *op = xzmalloc (sizeof (*op));

    op->type = type;

    return op;
}

static void op_destroy (op_t *op)
{
    switch (op->type) {
        case OP_NAME:
            if (op->u.name.key)
                free (op->u.name.key);
            break;
        case OP_STORE:
            if (op->u.store.ref)
                free (op->u.store.ref);
            break;
        case OP_FLUSH:
            if (op->u.flush.zmsg)
                zmsg_destroy (&op->u.flush.zmsg);
            break;
    }
    free (op);
}

static bool op_match (op_t *op1, op_t *op2)
{
    bool match = false;

    if (op1->type == op2->type) {
        switch (op1->type) {
            case OP_NAME:
                if (!strcmp (op1->u.name.key, op2->u.name.key))
                    match = true;
                break;
            case OP_STORE:
                if (!strcmp (op1->u.store.ref, op2->u.store.ref))
                    match = true;
                break;
            case OP_FLUSH:
                break;
        }
    }
    return match;
}

static void writeback_add_name (ctx_t *ctx, const char *key)
{
    op_t *op;

    //assert (!flux_treeroot (ctx->h));

    op = op_create (OP_NAME);
    op->u.name.key = xstrdup (key);
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
    ctx->slave.writeback_state = WB_DIRTY;
}

static void writeback_add_store (ctx_t *ctx, const char *ref)
{
    op_t *op;

    //assert (!flux_treeroot (ctx->h));

    op = op_create (OP_STORE);
    op->u.store.ref = xstrdup (ref);
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
    ctx->slave.writeback_state = WB_DIRTY;
}

static void writeback_add_flush (ctx_t *ctx, zmsg_t *zmsg)
{
    op_t *op;

    //assert (!flux_treeroot (ctx->h));

    op = op_create (OP_FLUSH);
    op->u.flush.zmsg = zmsg;
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
}

static void writeback_del (ctx_t *ctx, op_t *target)
{
    op_t *op = zlist_first (ctx->slave.writeback);

    while (op) {
        if (op_match (op, target))
            break;
        op = zlist_next (ctx->slave.writeback);
    }
    if (op) {
        zlist_remove (ctx->slave.writeback, op);
        op_destroy (op);
        /* handle flush(es) now at head of queue */
        while ((op = zlist_head (ctx->slave.writeback)) && op->type == OP_FLUSH) {
            ctx->slave.writeback_state = WB_FLUSHING;
            flux_request_sendmsg (ctx->h, &op->u.flush.zmsg); /* fwd upstream */
            zlist_remove (ctx->slave.writeback, op);
            op_destroy (op);
        }
    }
}

static hobj_t *hobj_create (json_object *o)
{
    hobj_t *hp = xzmalloc (sizeof (*hp));

    hp->o = o;
    hp->waitlist = wait_queue_create ();

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

    if (flux_treeroot (ctx->h)) {
        if (!hp)
            msg_exit ("dangling ref %s", ref);
    } else {
        if (!hp) {
            hp = hobj_create (NULL);
            zhash_insert (ctx->store, ref, hp);
            zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
            load_request_send (ctx, ref);
            /* leave hp->o == NULL to be filled in on response */
        }
        if (!hp->o) {
            if (w)
                wait_addqueue (hp->waitlist, w);
            done = false; /* stall */
        }
    }
    if (done) {
        assert (hp != NULL);
        assert (hp->o != NULL);
        *op = hp->o;
    }
    return done;
}

static void store_request_send (ctx_t *ctx, const href_t ref, json_object *val)
{
    json_object *o = util_json_object_new_object ();

    json_object_get (val);
    json_object_object_add (o, ref, val);
    flux_request_send (ctx->h, o, "kvs.store");
    json_object_put (o);
}

static void store (ctx_t *ctx, json_object *o, bool writeback, href_t ref)
{
    hobj_t *hp; 

    compute_json_href (o, ref);

    if ((hp = zhash_lookup (ctx->store, ref))) {
        if (hp->o) {
            json_object_put (o);
        } else {
            hp->o = o;
            wait_runqueue (hp->waitlist);
        }
    } else {
        hp = hobj_create (o);
        zhash_insert (ctx->store, ref, hp);
        zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
        if (writeback) {
            //assert (!flux_treeroot (ctx->h));
            writeback_add_store (ctx, ref);
            store_request_send (ctx, ref, o);
        }
    }
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
            store (ctx, o, false, nhref);
            json_object_object_add (cpy, iter.key,
                                        dirent_create ("FILEREF", nhref));
        } else { /* FILEVAL, FILEREF, DIRREF */
            json_object_get (iter.val);
            json_object_object_add (cpy, iter.key, iter.val);
        }
    }
    store (ctx, cpy, false, href);
}

static void name_request_send (ctx_t *ctx, char *key, json_object *dirent)
{
    json_object *o = util_json_object_new_object ();

    json_object_object_add (o, key, dirent);
    flux_request_send (ctx->h, o, "kvs.name");
    json_object_put (o);
}

/* consumes dirent */
static void name (ctx_t *ctx, char *key, json_object *dirent,
                  bool writeback)
{
    if (writeback) {
        writeback_add_name (ctx, key);
        name_request_send (ctx, key, dirent);
    } else {
        name_t *np = xzmalloc (sizeof (*np));
        np->key = xstrdup (key);
        np->dirent = dirent;
        if (zlist_append (ctx->master.namequeue, np) < 0)
            oom ();
    }
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
    }
}

/* Put name to deep copy of root directory.  Consumes np.
 */
static void deep_put (json_object *dir, name_t *np)
{
    char *next, *name = np->key;
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
    if (np->dirent)
        json_object_object_add (dir, name, np->dirent); /* consumes dirent */
    free (np->key);
    free (np);
}

static void update_version (ctx_t *ctx, int newvers)
{
    json_object *o;

    //assert (flux_treeroot (ctx->h));

    if (!(o = json_object_new_int (newvers)))
        oom ();
    name (ctx, "version", dirent_create ("FILEVAL", o), false);
    json_object_put (o);
}

/* Read the entire hierarchy of KVS directories into a json object,
 * apply metdata updates from master.namequeue to it, then put the json
 * object back to the store and update the root directory reference.
 * FIXME: garbage collect unreferenced store entries (refcount them)
 * FIXME: only read in directories affected by the metadata updates
 */
static void commit (ctx_t *ctx)
{
    name_t *np;
    json_object *dir, *cpy;
    href_t ref;

    //assert (flux_treeroot (ctx->h));

    (void)load (ctx, ctx->rootdir, NULL, &dir);
    assert (dir != NULL);
    cpy = kvs_save (ctx, dir, NULL, KVS_GET_DIRVAL);
    assert (cpy != NULL);
    update_version (ctx, ctx->rootseq + 1);
    while ((np = zlist_pop (ctx->master.namequeue))) {
        deep_put (cpy, np); /* destroys np */
    }
    kvs_restore (ctx, cpy, ref);
    json_object_put (cpy);
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
    href_t href;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        store (ctx, iter.val, false, href);
        if (strcmp (href, iter.key) != 0)
            flux_log (ctx->h, LOG_ERR, "%s: bad href %s", __FUNCTION__, iter.key);
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
    bool writeback = !flux_treeroot (ctx->h);
    href_t href;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        store (ctx, iter.val, writeback, href);
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
    op_t target = { .type = OP_STORE };
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        target.u.store.ref = iter.key;
        writeback_del (ctx, &target);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int clean_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    int s1, s2;
    int rc = 0;
    json_object *rootdir, *cpy;
    href_t href;

    if ((!flux_treeroot (ctx->h) && zlist_size (ctx->slave.writeback) > 0) ||
          (flux_treeroot (ctx->h) && zlist_size (ctx->master.namequeue) > 0)) {
        flux_log (ctx->h, LOG_ALERT, "cache is busy");
        rc = EAGAIN;
        goto done;
    }
    s1 = zhash_size (ctx->store);
    if (flux_treeroot (ctx->h)) {
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
    } else {
        zhash_destroy (&ctx->store);
        if (!(ctx->store = zhash_new ()))
            oom ();
    }
    s2 = zhash_size (ctx->store);
    flux_log (ctx->h, LOG_ALERT, "dropped %d of %d cache entries", s1 - s2, s1);
done:
    flux_respond_errnum (ctx->h, zmsg, rc);
    return 0;
}

static int name_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *cpy = NULL, *o = NULL;
    json_object_iter iter;
    bool writeback = !flux_treeroot (ctx->h);

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (iter.val)
            json_object_get (iter.val);
        name (ctx, iter.key, iter.val, writeback);
        json_object_object_del (cpy, iter.key);
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

static int name_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    json_object_iter iter;
    op_t target = { .type = OP_NAME };
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        target.u.name.key = iter.key;
        writeback_del (ctx, &target);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int flush_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    if (flux_treeroot (ctx->h) || ctx->slave.writeback_state == WB_CLEAN)
        flux_respond_errnum (ctx->h, zmsg, 0);
    else if (zlist_size (ctx->slave.writeback) == 0) {
        flux_request_sendmsg (ctx->h, zmsg); /* fwd upstream */
        ctx->slave.writeback_state = WB_FLUSHING;
    } else {
        writeback_add_flush (ctx, *zmsg); /* enqueue */
        *zmsg = NULL;
    }
    return 0;
}

static int flush_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    flux_response_sendmsg (h, zmsg); /* fwd downstream */
    if (ctx->slave.writeback_state == WB_FLUSHING)
        ctx->slave.writeback_state = WB_CLEAN;
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
            msg_exit ("corrupt internal storage");
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
        if (!walk (ctx, root, name, &dirent, w, readlink, 0))
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
        if (!flux_treeroot (ctx->h) && !readahead_dir (ctx, val, w, dir_flags))
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

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
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
done:
    if (o)
        json_object_put (o);
    if (reply)
        json_object_put (reply);
    if (*zmsg)
        zmsg_destroy (zmsg);
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
    bool writeback = !flux_treeroot (ctx->h);
    bool flag_mkdir = false;
    bool flag_symlink = false;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
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
                store (ctx, empty_dir, writeback, ref);
                name (ctx, iter.key, dirent_create ("DIRREF", ref), writeback);
            } else
                name (ctx, iter.key, NULL, writeback);
        } else if (flag_symlink) {
            name (ctx, iter.key, dirent_create ("LINKVAL", iter.val), writeback);
        } else if (store_by_reference (iter.val)) {
            json_object_get (iter.val);
            store (ctx, iter.val, writeback, ref);
            name (ctx, iter.key, dirent_create ("FILEREF", ref), writeback);
        } else {
            name (ctx, iter.key, dirent_create ("FILEVAL", iter.val), writeback);
        }
    }
    flux_respond_errnum (h, zmsg, 0); /* success */
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static void commit_request_send (ctx_t *ctx, const char *name)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "name", name);
    flux_request_send (ctx->h, o, "kvs.commit");
    json_object_put (o);
}

static void commit_response_send (ctx_t *ctx, commit_t *cp, 
                                  json_object *o, zmsg_t **zmsg)
{
    char *rootref = encode_rootref (cp->rootseq, cp->rootdir);

    util_json_object_add_string (o, "rootref", rootref);
    flux_respond (ctx->h, zmsg, o);
    free (rootref);
}

static int commit_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    const char *name;
    commit_t *cp;
    wait_t w;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    w = wait_create (h, typemask, zmsg, commit_request_cb, arg);
    if (flux_treeroot (ctx->h)) {
        if (!(cp = commit_find (ctx, name))) {
            commit (ctx);
            cp = commit_new (ctx, name);
            commit_done (ctx, cp);
            (void)setroot_event_send (ctx);
        }
    } else {
        if (!(cp = commit_find (ctx, name))) {
            commit_request_send (ctx, name);
            cp = commit_new (ctx, name);
        }
        if (!cp->done) {
            wait_addqueue (cp->waitlist, w);
            goto done; /* stall */
        }
    }
    wait_destroy (w, zmsg);
    commit_response_send (ctx, cp, o, zmsg);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int commit_response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    const char *rootref, *name;
    int seq;
    href_t href;
    commit_t *cp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0
            || util_json_object_get_string (o, "rootref", &rootref) < 0
            || !decode_rootref (rootref, &seq, href)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    setroot (ctx, seq, href); /* may be redundant - racing with multicast */
    cp = commit_find (ctx, name);
    assert (cp != NULL);
    commit_done (ctx, cp);
    wait_runqueue (cp->waitlist);
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
        free (sender);
    }
    zmsg_destroy (zmsg);
    return 0;
}

static void setargs (ctx_t *ctx, zhash_t *args)
{
    zlist_t *keys = zhash_keys (args);
    char *key, *val;
    json_object *vo;
    href_t ref;

    while ((key = zlist_pop (keys))) {
        val = zhash_lookup (args, key);
        if (!(vo = json_tokener_parse (val)))
            vo = json_object_new_string (val);
        if (!vo)
            continue;
        if (store_by_reference (vo)) {
            store (ctx, vo, false, ref);
            name (ctx, key, dirent_create ("FILEREF", ref), false);
        } else {
            name (ctx, key, dirent_create ("FILEVAL", vo), false);
        }
    }
    zlist_destroy (&keys);
    commit (ctx);
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "event.kvs.setroot",    setroot_event_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.getroot",          getroot_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.clean",            clean_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.get",              get_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.watch",            watch_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.put",              put_request_cb },
    { FLUX_MSGTYPE_REQUEST, "kvs.disconnect",       disconnect_request_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.load",             load_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.load",             load_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.store",            store_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.store",            store_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.name",             name_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.name",             name_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.flush",            flush_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.flush",            flush_response_cb },

    { FLUX_MSGTYPE_REQUEST, "kvs.commit",           commit_request_cb },
    { FLUX_MSGTYPE_RESPONSE,"kvs.commit",           commit_response_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int kvssrv_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    bool treeroot = flux_treeroot (h);

    if (!treeroot) {
        if (flux_event_subscribe (h, "event.kvs.setroot") < 0) {
            err ("%s: flux_event_subscribe", __FUNCTION__);
            return -1;
        }
    }
    if (flux_event_subscribe (h, "event.kvs.debug.") < 0) {
        err ("%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }

    if (treeroot) {
        json_object *rootdir = util_json_object_new_object ();
        href_t href;

        store (ctx, rootdir, false, href);
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
