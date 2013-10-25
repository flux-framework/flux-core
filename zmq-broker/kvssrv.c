/* kvssrv.c - distributed key-value store based on hash tree */

/* JSON directory object: 
 * list of key-value pairs where key is a name, value is a dirent
 *
 * JSON dirent objects:
 * object containing one key-value pair where key is one of
 * "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", and value is a SHA1
 * hash key into ctx->store (FILEREF, DIRREF), or an actual directory
 * or file (value) JSON object (FILEVAL, DIRVAL).
 *
 * For example, consider KVS containing:
 * a="foo"
 * b="bar"
 * c.d="baz"
 *
 * Root directory:
 * {"a":{"FILEREF":"f1d2d2f924e986ac86fdf7b36c94bcdf32beec15"},
 *  "b":{"FILEREF","8714e0ef31edb00e33683f575274379955b3526c"},
 *  "c":{"DIRREF","6eadd3a778e410597c85d74c287a57ad66071a45"}}
 *
 * Deep copy of root directory:
 * {"a":{"FILEVAL":"foo"},
 *  "b":{"FILEVAL","bar"},
 *  "c":{"DIRVAL",{"d":{"FILEVAL":"baz"}}}}
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
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

/* Large values are stored in dirents by reference; small values by value.
 */
#define LARGE_VAL 100

/* Break cycles in symlink references.
 */
#define SYMLINK_CYCLE_LIMIT 10

typedef void (*wait_fun_t) (plugin_ctx_t *p, char *arg, zmsg_t **zmsg);
typedef struct {
    plugin_ctx_t *p;
    int usecount;
    wait_fun_t fun;    
    char *arg;
    zmsg_t *zmsg;
    char *id;
} wait_t;

typedef struct {
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
    zlist_t *waitlist;
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
    zlist_t *watchlist;
    struct {
        zlist_t *namequeue;
    } master;
    struct {
        zlist_t *writeback;
        enum { WB_CLEAN, WB_FLUSHING, WB_DIRTY } writeback_state;
    } slave;
} ctx_t;

static void event_kvs_setroot_send (plugin_ctx_t *p);

static wait_t *wait_create (plugin_ctx_t *p, wait_fun_t fun,
                            char *arg, zmsg_t **zmsg)
{
    wait_t *wp = xzmalloc (sizeof (*wp));
    wp->usecount = 0;
    wp->fun = fun;
    if (arg)
        wp->arg = xstrdup (arg);
    wp->zmsg = *zmsg;
    *zmsg = NULL;
    wp->p = p;

    return wp;
}

static void wait_set_id (wait_t *wp, char *id)
{
    if (wp->id)
        free (wp->id);
    wp->id = xstrdup (id);
}

static void wait_destroy (wait_t *wp, zmsg_t **zmsg)
{
    assert (zmsg != NULL || wp->zmsg == NULL);
    if (wp->arg)
        free (wp->arg);
    if (wp->id)
        free (wp->id);
    if (zmsg)
        *zmsg = wp->zmsg;
    free (wp);
}

static void wait_destroy_byid (zlist_t **list, char *id)
{
    zlist_t *tmp;
    wait_t *wp;
    zmsg_t *zmsg;

    if (!(tmp = zlist_new ()))
        oom ();
    while ((wp = zlist_pop (*list))) {
        if (wp->id && strcmp (wp->id, id) == 0) {
            wait_destroy (wp, &zmsg); 
            if (zmsg)
                zmsg_destroy (&zmsg);
        } else
            if (zlist_push (tmp, wp) < 0)
                oom ();
    }
    zlist_destroy (list);
    *list = tmp;
}

static void wait_add (zlist_t *waitlist, wait_t *wp)
{
    if (zlist_append (waitlist, wp) < 0)
        oom ();
    wp->usecount++;
}

/* Take all waiters off a waitlist, copying them first to a temporary
 * list so that they can put themselves back on if they want (e.g. watch)
 */
static void wait_del_all (zlist_t *waitlist)
{
    zlist_t *cpy;
    wait_t *wp;

    if (!(cpy = zlist_new ()))
        oom ();
    while ((wp = zlist_pop (waitlist)))
        if (zlist_append (cpy, wp) < 0)
            oom ();
    while ((wp = zlist_pop (cpy))) {
        if (--wp->usecount == 0) {
            wp->fun (wp->p, wp->arg, &wp->zmsg);
            wait_destroy (wp, NULL);
        }
    }
    zlist_destroy (&cpy);
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
    if (!(cp->waitlist = zlist_new ()))
        oom ();
    return cp;
}

static void commit_destroy (commit_t *cp)
{
    assert (zlist_size (cp->waitlist) == 0);
    zlist_destroy (&cp->waitlist);
    free (cp);
}

static commit_t *commit_new (plugin_ctx_t *p, const char *name)
{
    ctx_t *ctx = p->ctx;
    commit_t *cp = commit_create ();

    zhash_insert (ctx->commits, name, cp);
    zhash_freefn (ctx->commits, name, (zhash_free_fn *)commit_destroy);
    return cp;
}

static commit_t *commit_find (plugin_ctx_t *p, const char *name)
{
    ctx_t *ctx = p->ctx;
    return zhash_lookup (ctx->commits, name);
}

static void commit_done (plugin_ctx_t *p, commit_t *cp)
{
    ctx_t *ctx = p->ctx;

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

static void writeback_add_name (plugin_ctx_t *p, const char *key)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_NAME);
    op->u.name.key = xstrdup (key);
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
    ctx->slave.writeback_state = WB_DIRTY;
}

static void writeback_add_store (plugin_ctx_t *p, const char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_STORE);
    op->u.store.ref = xstrdup (ref);
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
    ctx->slave.writeback_state = WB_DIRTY;
}

static void writeback_add_flush (plugin_ctx_t *p, zmsg_t *zmsg)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_FLUSH);
    op->u.flush.zmsg = zmsg;
    if (zlist_append (ctx->slave.writeback, op) < 0)
        oom ();
}

static void writeback_del (plugin_ctx_t *p, op_t *target)
{
    ctx_t *ctx = p->ctx;
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
            plugin_send_request_raw (p, &op->u.flush.zmsg); /* fwd upstream */
            zlist_remove (ctx->slave.writeback, op);
            op_destroy (op);
        }
    }
}

static hobj_t *hobj_create (json_object *o)
{
    hobj_t *hp = xzmalloc (sizeof (*hp));

    hp->o = o;
    if (!(hp->waitlist = zlist_new ()))
        oom ();

    return hp;
}

static void hobj_destroy (hobj_t *hp)
{
    if (hp->o)
        json_object_put (hp->o);
    assert (zlist_size (hp->waitlist) == 0);
    zlist_destroy (&hp->waitlist);
    free (hp);
}

static void load_request_send (plugin_ctx_t *p, const href_t ref)
{
    json_object *o = util_json_object_new_object ();

    json_object_object_add (o, ref, NULL);
    plugin_send_request (p, o, "kvs.load");
    json_object_put (o);
}

static bool load (plugin_ctx_t *p, const href_t ref, wait_t *wp,
                  json_object **op)
{
    ctx_t *ctx = p->ctx;
    hobj_t *hp = zhash_lookup (ctx->store, ref);
    bool done = true;

    if (plugin_treeroot (p)) {
        if (!hp)
            msg_exit ("dangling ref %s", ref);
    } else {
        if (!hp) {
            hp = hobj_create (NULL);
            zhash_insert (ctx->store, ref, hp);
            zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
            load_request_send (p, ref);
            /* leave hp->o == NULL to be filled in on response */
        }
        if (!hp->o) {
            if (wp)
                wait_add (hp->waitlist, wp);
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

static void store_request_send (plugin_ctx_t *p, const href_t ref,
                                json_object *val)
{
    json_object *o = util_json_object_new_object ();

    json_object_get (val);
    json_object_object_add (o, ref, val);
    plugin_send_request (p, o, "kvs.store");
    json_object_put (o);
}

static void store (plugin_ctx_t *p, json_object *o, bool writeback, href_t ref)
{
    ctx_t *ctx = p->ctx;
    hobj_t *hp; 

    compute_json_href (o, ref);

    if ((hp = zhash_lookup (ctx->store, ref))) {
        if (hp->o) {
            json_object_put (o);
        } else {
            hp->o = o;
            wait_del_all (hp->waitlist);
        }
    } else {
        hp = hobj_create (o);
        zhash_insert (ctx->store, ref, hp);
        zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
        if (writeback) {
            assert (!plugin_treeroot (p));
            writeback_add_store (p, ref);
            store_request_send (p, ref, o);
        }
    }
}

static bool readahead_dir (plugin_ctx_t *p, json_object *dir, wait_t *wp,
                          int flags)
{
    json_object *val;
    json_object_iter iter;
    const char *ref;
    bool done = true;

    json_object_object_foreachC (dir, iter) {
        if ((flags & KVS_GET_FILEVAL) && util_json_object_get_string (iter.val, "FILEREF", &ref) == 0) {
            if (!load (p, ref, wp, &val)) {
                done = false;
                continue;
            }
        } else if ((flags & KVS_GET_DIRVAL) && util_json_object_get_string (iter.val, "DIRREF", &ref) == 0){
            if (!load (p, ref, wp, &val)) {
                done = false;
                continue;
            }
            if (!readahead_dir (p, val, wp, flags))
                done = false;
        }
    }
    return done;
}

static json_object *kvs_copy (plugin_ctx_t *p, json_object *dir, wait_t *wp,
                              int flags)
{
    json_object *dcpy, *val;
    json_object_iter iter;
    const char *ref;

    if (!(dcpy = json_object_new_object ()))
        oom ();
    
    json_object_object_foreachC (dir, iter) {
        if ((flags & KVS_GET_FILEVAL)
                && util_json_object_get_string (iter.val, "FILEREF", &ref)==0) {
            if (!load (p, ref, wp, &val))
                goto stall;
            json_object_object_add (dcpy, iter.key,
                                    dirent_create ("FILEVAL", val));
        } else if ((flags & KVS_GET_DIRVAL)
                && util_json_object_get_string (iter.val, "DIRREF", &ref)==0) {
            if (!load (p, ref, wp, &val))
                goto stall;
            if (!(val = kvs_copy (p, val, wp, flags)))
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


static void name_request_send (plugin_ctx_t *p, char *key, json_object *dirent)
{
    json_object *o = util_json_object_new_object ();

    json_object_object_add (o, key, dirent);
    plugin_send_request (p, o, "kvs.name");
    json_object_put (o);
}

/* consumes dirent */
static void name (plugin_ctx_t *p, char *key, json_object *dirent,
                  bool writeback)
{
    ctx_t *ctx = p->ctx;

    if (writeback) {
        writeback_add_name (p, key);
        name_request_send (p, key, dirent);
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

static void setroot (plugin_ctx_t *p, int seq, href_t ref)
{
    ctx_t *ctx = p->ctx;

    if (seq == 0 || seq > ctx->rootseq) {
        memcpy (ctx->rootdir, ref, sizeof (href_t));
        ctx->rootseq = seq;
        wait_del_all (ctx->watchlist);
    }
}

/* Store unwound deep copy of root directory and return its href.
 */
static void deep_unwind (plugin_ctx_t *p, json_object *dir, href_t href)
{
    json_object *cpy, *o;
    json_object_iter iter;
    href_t nhref;

    if (!(cpy = json_object_new_object ()))
        oom ();

    json_object_object_foreachC (dir, iter) {
        if ((o = json_object_object_get (iter.val, "DIRVAL"))) {
            deep_unwind (p, o, nhref);
            json_object_object_add (cpy, iter.key,
                                    dirent_create ("DIRREF", nhref));
        } else { /* FILEVAL, LINKVAL, FILEREF, DIRREF */
            json_object_get (iter.val);
            json_object_object_add (cpy, iter.key, iter.val);
        }
    }
    store (p, cpy, false, href);
}

/* Put name to deep copy of root directory.  Consumes np.
 *  (assumes deep copy was created with nf=true)
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

static void commit (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    name_t *np;
    json_object *dir, *cpy;
    href_t ref;

    assert (plugin_treeroot (p));

    (void)load (p, ctx->rootdir, NULL, &dir);
    assert (dir != NULL);
    cpy = kvs_copy (p, dir, NULL, KVS_GET_DIRVAL);
    assert (cpy != NULL);
    while ((np = zlist_pop (ctx->master.namequeue))) {
        deep_put (cpy, np); /* destroys np */
    }
    deep_unwind (p, cpy, ref);
    json_object_put (cpy);
    setroot (p, ctx->rootseq + 1, ref);
}

static void kvs_load (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    json_object *val, *cpy = NULL, *o = NULL;
    json_object_iter iter;
    wait_t *wp = NULL;
    bool stall = false;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_load, arg, zmsg);
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!load (p, iter.key, wp, &val))
            stall = true;
        else {
            json_object_get (val);
            json_object_object_add (cpy, iter.key, val);
        }
    }
    if (!stall) {
        wait_destroy (wp, zmsg);
        plugin_send_response (p, zmsg, cpy);
    }
done:
    if (o)
        json_object_put (o);
    if (cpy)
        json_object_put (cpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_load_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    href_t href;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        store (p, iter.val, false, href);
        if (strcmp (href, iter.key) != 0)
            plugin_log (p, LOG_ERR, "%s: bad href %s", __FUNCTION__, iter.key);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_store (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    json_object *cpy = NULL, *o = NULL;
    json_object_iter iter;
    bool writeback = !plugin_treeroot (p);
    href_t href;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        json_object_get (iter.val);
        store (p, iter.val, writeback, href);
        if (strcmp (href, iter.key) != 0)
            plugin_log (p, LOG_ERR, "%s: bad href %s", __FUNCTION__, iter.key);
        json_object_object_add (cpy, iter.key, NULL);
    }
    plugin_send_response (p, zmsg, cpy);
done:
    if (o)
        json_object_put (o);
    if (cpy)
        json_object_put (cpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_store_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    op_t target = { .type = OP_STORE };
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        target.u.store.ref = iter.key;
        writeback_del (p, &target);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_clean_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int s1, s2;
    int rc = 0;
    json_object *rootdir, *cpy;
    href_t href;

    if ((!plugin_treeroot (p) && zlist_size (ctx->slave.writeback) > 0) ||
          (plugin_treeroot (p) && zlist_size (ctx->master.namequeue) > 0)) {
        plugin_log (p, LOG_ALERT, "cache is busy");
        rc = EAGAIN;
        goto done;
    }
    s1 = zhash_size (ctx->store);
    if (plugin_treeroot (p)) {
        (void)load (p, ctx->rootdir, NULL, &rootdir);
        assert (rootdir != NULL);
        cpy = kvs_copy (p, rootdir, NULL, KVS_GET_DIRVAL | KVS_GET_FILEVAL);
        assert (cpy != NULL);
        zhash_destroy (&ctx->store);
        if (!(ctx->store = zhash_new ()))
            oom ();
        deep_unwind (p, cpy, href);
        assert (!strcmp (ctx->rootdir, href));
        json_object_put (cpy);
    } else {
        zhash_destroy (&ctx->store);
        if (!(ctx->store = zhash_new ()))
            oom ();
    }
    s2 = zhash_size (ctx->store);
    plugin_log (p, LOG_ALERT, "dropped %d of %d cache entries", s1 - s2, s1);
done:
    plugin_send_response_errnum (p, zmsg, rc);
}

static void kvs_name (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    json_object *cpy = NULL, *o = NULL;
    json_object_iter iter;
    bool writeback = !plugin_treeroot (p);

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (iter.val) {
            json_object_get (iter.val);
            name (p, iter.key, iter.val, writeback);
            json_object_object_del (cpy, iter.key);
            json_object_object_add (cpy, iter.key, NULL);
        } else 
            name (p, iter.key, NULL, writeback);
    }
    plugin_send_response (p, zmsg, cpy);
done:
    if (o)
        json_object_put (o);
    if (cpy)
        json_object_put (cpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_name_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    op_t target = { .type = OP_NAME };
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        target.u.name.key = iter.key;
        writeback_del (p, &target);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_flush_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    if (plugin_treeroot (p) || ctx->slave.writeback_state == WB_CLEAN)
        plugin_send_response_errnum (p, zmsg, 0);
    else if (zlist_size (ctx->slave.writeback) == 0) {
        plugin_send_request_raw (p, zmsg); /* fwd upstream */
        ctx->slave.writeback_state = WB_FLUSHING;
    } else {
        writeback_add_flush (p, *zmsg); /* enqueue */
        *zmsg = NULL;
    }
}

static void kvs_flush_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    plugin_send_response_raw (p, zmsg); /* fwd downstream */
    if (ctx->slave.writeback_state == WB_FLUSHING)
        ctx->slave.writeback_state = WB_CLEAN;
}

/* Get dirent containing requested key.
 */
static bool walk (plugin_ctx_t *p, json_object *root, const char *path,
                  json_object **direntp, wait_t *wp, bool readlink, int depth)
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
        /* always deref symlinks in non-terminal directories */
        if (util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
            if (depth == SYMLINK_CYCLE_LIMIT)
                goto error; /* FIXME: get ELOOP back to kvs_get */
            if (!walk (p, root, link, &dirent, wp, readlink, depth))
                goto stall;        
        }
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (!load (p, ref, wp, &dir))
                goto stall;
        } else {
            goto error;
        }
        name = next;
    }
    /* now terminal path component */
    dirent = json_object_object_get (dir, name);
    /* if symlink, deref unless 'readlink' flag is set */
    if (dirent && !readlink
               && util_json_object_get_string (dirent, "LINKVAL", &link) == 0) {
        if (depth == SYMLINK_CYCLE_LIMIT)
            goto error; /* FIXME: get ELOOP back to kvs_get */
        if (!walk (p, root, link, &dirent, wp, readlink, depth))
            goto stall;
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

static bool lookup (plugin_ctx_t *p, json_object *root, wait_t *wp,
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
        if (!walk (p, root, name, &dirent, wp, readlink, 0))
            goto stall;
        if (!dirent) {
            //errnum = ENOENT;
            goto done; /* a NULL response is not necessarily an error */
        }
        if (util_json_object_get_string (dirent, "DIRREF", &ref) == 0) {
            if (!dir) {
                errnum = EISDIR;
                goto done;
            }
            if (!load (p, ref, wp, &val))
                goto stall;
            isdir = true;
        } else if (util_json_object_get_string (dirent, "FILEREF", &ref) == 0) {
            if (dir) {
                errnum = ENOTDIR;
                goto done;
            }
            if (!load (p, ref, wp, &val))
                goto stall;
        } else if ((vp = json_object_object_get (dirent, "DIRVAL"))) {
            if (!dir) {
                errnum = EISDIR;
                goto done;
            }
            val = vp;
            isdir = true;
        } else if ((vp = json_object_object_get (dirent, "FILEVAL"))) {
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
        if (!plugin_treeroot (p) && !readahead_dir (p, val, wp, dir_flags))
            goto stall;
        if (!(val = kvs_copy (p, val, wp, dir_flags)))
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

static void kvs_get_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *root, *reply = NULL, *o = NULL;
    json_object_iter iter;
    wait_t *wp = NULL;
    bool stall = false;
    bool flag_directory = false;
    bool flag_dirval = false;
    bool flag_fileval = false;
    bool flag_readlink = false;
    int dir_flags = 0;
    int errnum = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_get_request, arg, zmsg);
    if (!load (p, ctx->rootdir, wp, &root)) {
        stall = true;
        goto done;
    }
    /* handle flags - they apply to all keys in the request */
    (void)util_json_object_get_boolean (o, ".flag_directory", &flag_directory);
    (void)util_json_object_get_boolean (o, ".flag_dirval", &flag_dirval);
    (void)util_json_object_get_boolean (o, ".flag_fileval", &flag_fileval);
    (void)util_json_object_get_boolean (o, ".flag_readlink", &flag_readlink);
    if (flag_dirval)
        dir_flags |= KVS_GET_DIRVAL;
    if (flag_fileval)
        dir_flags |= KVS_GET_FILEVAL;

    reply = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6)) /* ignore flags */
            continue;
        json_object *val = NULL;
        if (!lookup (p, root, wp, flag_directory, dir_flags, flag_readlink,
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
        wait_destroy (wp, zmsg); /* get back *zmsg */
        plugin_send_response_errnum (p, zmsg, errnum);
    } else if (!stall) {
        wait_destroy (wp, zmsg); /* get back *zmsg */
        (void)cmb_msg_replace_json (*zmsg, reply);
        plugin_send_response_raw (p, zmsg);
    }
done:
    if (o)
        json_object_put (o);
    if (reply)
        json_object_put (reply);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_watch_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    char *sender = cmb_msg_sender (*zmsg);
    ctx_t *ctx = p->ctx;
    json_object *root, *reply = NULL, *o = NULL;
    json_object_iter iter;
    wait_t *wp = NULL;
    bool stall = false, changed = false;
    bool flag_directory = false, flag_continue = false;
    bool flag_dirval = false, flag_fileval = false;
    bool flag_readlink = false;
    int dir_flags = 0;
    int errnum = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_watch_request, arg, zmsg);
    if (!load (p, ctx->rootdir, wp, &root)) {
        stall = true;
        goto done;
    }
    /* handle flags - they apply to all keys in the request */
    (void)util_json_object_get_boolean (o, ".flag_directory", &flag_directory);
    (void)util_json_object_get_boolean (o, ".flag_continue", &flag_continue);
    (void)util_json_object_get_boolean (o, ".flag_dirval", &flag_dirval);
    (void)util_json_object_get_boolean (o, ".flag_fileval", &flag_fileval);
    (void)util_json_object_get_boolean (o, ".flag_readlink", &flag_readlink);
    if (flag_dirval)
        dir_flags |= KVS_GET_DIRVAL;
    if (flag_fileval)
        dir_flags |= KVS_GET_FILEVAL;

    reply = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (!strncmp (iter.key, ".flag_", 6)) /* ignore flags */
            continue;
        json_object *val = NULL;
        if (!lookup (p, root, wp, flag_directory, dir_flags, flag_readlink,
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
        wait_destroy (wp, zmsg); /* get back *zmsg */
        plugin_send_response_errnum (p, zmsg, errnum);
    } else if (!stall) {
        zmsg_t *zcpy;
        wait_destroy (wp, zmsg); /* get back *zmsg */
        (void)cmb_msg_replace_json (*zmsg, reply);

        /* Prepare to resubmit the request on the watchlist for future changes.
         * Values were updated above.  Flags are reattached here.
         * We set the 'continue' flag for next time so that a reply is
         * only sent if value changes.
         */
        if (!(zcpy = zmsg_dup (*zmsg)))
            oom ();
        util_json_object_add_boolean (reply, ".flag_directory", flag_directory);
        util_json_object_add_boolean (reply, ".flag_dirval", flag_dirval);
        util_json_object_add_boolean (reply, ".flag_fileval", flag_fileval);
        util_json_object_add_boolean (reply, ".flag_readlink", flag_readlink);
        util_json_object_add_boolean (reply, ".flag_continue", true);
        (void)cmb_msg_replace_json (zcpy, reply);

        /* On every commit, kvs_watch_request (p, arg, zcpy) will be called.
         * No reply will be generated unless a value has changed.
         */
        wp = wait_create (p, kvs_watch_request, arg, &zcpy);
        wait_set_id (wp, sender);
        wait_add (ctx->watchlist, wp);

        /* Reply to the watch request.
         * The initial request always gets a reply.
         */
        if (changed || !flag_continue)
            plugin_send_response_raw (p, zmsg);
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
}

static void kvs_put_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    href_t ref;
    bool writeback = !plugin_treeroot (p);
    bool flag_mkdir = false;
    bool flag_symlink = false;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
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
                store (p, empty_dir, writeback, ref);
                name (p, iter.key, dirent_create ("DIRREF", ref), writeback);
            } else
                name (p, iter.key, NULL, writeback);
        } else if (flag_symlink) {
            name (p, iter.key, dirent_create ("LINKVAL", iter.val), writeback);
        } else if (strlen (json_object_to_json_string (iter.val)) < LARGE_VAL) {
            name (p, iter.key, dirent_create ("FILEVAL", iter.val), writeback);
        } else {
            json_object_get (iter.val);
            store (p, iter.val, writeback, ref);
            name (p, iter.key, dirent_create ("FILEREF", ref), writeback);
        }
    }
    plugin_send_response_errnum (p, zmsg, 0); /* success */
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void commit_request_send (plugin_ctx_t *p, const char *name)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "name", name);
    plugin_send_request (p, o, "kvs.commit");
    json_object_put (o);
}

static void commit_response_send (plugin_ctx_t *p, commit_t *cp, 
                                  json_object *o, zmsg_t **zmsg)
{
    char *rootref = encode_rootref (cp->rootseq, cp->rootdir);

    util_json_object_add_string (o, "rootref", rootref);
    plugin_send_response (p, zmsg, o);
    free (rootref);
}

static void kvs_commit_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    json_object *o = NULL;
    const char *name;
    commit_t *cp;
    wait_t *wp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_commit_request, arg, zmsg);
    if (plugin_treeroot (p)) {
        if (!(cp = commit_find (p, name))) {
            commit (p);
            cp = commit_new (p, name);
            commit_done (p, cp);
            event_kvs_setroot_send (p);
        }
    } else {
        if (!(cp = commit_find (p, name))) {
            commit_request_send (p, name);
            cp = commit_new (p, name);
        }
        if (!cp->done) {
            wait_add (cp->waitlist, wp);
            goto done; /* stall */
        }
    }
    wait_destroy (wp, zmsg);
    commit_response_send (p, cp, o, zmsg);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_commit_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    const char *arg, *name;
    int seq;
    href_t href;
    commit_t *cp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0
            || util_json_object_get_string (o, "rootref", &arg) < 0
            || !decode_rootref (arg, &seq, href)) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    setroot (p, seq, href); /* may be redundant - racing with multicast */
    cp = commit_find (p, name);
    assert (cp != NULL);
    commit_done (p, cp);
    wait_del_all (cp->waitlist);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_getroot_request (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o;
    char *rootref = encode_rootref (ctx->rootseq, ctx->rootdir);

    if (!(o = json_object_new_string (rootref)))
        oom ();
    plugin_send_response (p, zmsg, o);
    free (rootref);
    json_object_put (o);
}

static void event_kvs_setroot (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    href_t href;
    int seq;

    assert (!plugin_treeroot (p));

    if (!decode_rootref (arg, &seq, href))
        plugin_log (p, LOG_ERR, "%s: malformed rootref %s", __FUNCTION__, arg);
    else
        setroot (p, seq, href);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void event_kvs_setroot_send (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    char *rootref = encode_rootref (ctx->rootseq, ctx->rootdir);
    plugin_send_event (p, "event.kvs.setroot.%s", rootref);
    free (rootref);
}

static int log_store_entry (const char *key, void *item, void *arg)
{
    plugin_ctx_t *p = arg;
    hobj_t *hp = item; 

    plugin_log (p, LOG_DEBUG, "%s\t%s [%d waiters]", key,
                hp->o ? json_object_to_json_string (hp->o) : "<unset>",
                zlist_size (hp->waitlist));
    return 0;
}

static void event_kvs_debug (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    if (!strcmp (arg, "writeback.size"))
        plugin_log (p, LOG_DEBUG, "writeback %d",
                    zlist_size (ctx->slave.writeback));
    else if (!strcmp (arg, "store.size"))
        plugin_log (p, LOG_DEBUG, "store %d", zhash_size (ctx->store));
    else if (!strcmp (arg, "root"))
        plugin_log (p, LOG_DEBUG, "root %d,%s", ctx->rootseq, ctx->rootdir);
    else if (!strcmp (arg, "store"))
        zhash_foreach (ctx->store, log_store_entry, p);
    else if (!strcmp (arg, "commit")) {
        zlist_t *keys = zhash_keys (ctx->commits);
        commit_t *cp = zlist_first (keys);
        int done = 0;
        while (cp) {
            if (cp->done)
                done++;
            cp = zlist_next (keys);
        }
        zlist_destroy (&keys);
        plugin_log (p, LOG_DEBUG, "commits %d/%d complete",
                    done, zhash_size (ctx->commits));
    }
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_disconnect (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    char *sender = cmb_msg_sender (*zmsg);
    ctx_t *ctx = p->ctx;

    if (sender) {
        wait_destroy_byid (&ctx->watchlist, sender);
        free (sender);
    }
    zmsg_destroy (zmsg);
}

static void kvs_recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;

    if (cmb_msg_match (*zmsg, "kvs.getroot")) {
        kvs_getroot_request (p, NULL, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.kvs.setroot.", &arg)) {
        event_kvs_setroot (p, arg, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.kvs.debug.", &arg)) {
        event_kvs_debug (p, arg, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.clean")) {
        kvs_clean_request (p, NULL, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.get")) {
        kvs_get_request (p, NULL, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.watch")) {
        kvs_watch_request (p, NULL, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.put")) {
        kvs_put_request (p, NULL, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.disconnect")) {
        kvs_disconnect (p, NULL, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.load")) {
        if (type == ZMSG_REQUEST)
            kvs_load (p, NULL, zmsg);
        else
            kvs_load_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.store")) {
        if (type == ZMSG_REQUEST)
            kvs_store (p, NULL, zmsg);
        else
            kvs_store_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.name")) {
        if (type == ZMSG_REQUEST)
            kvs_name (p, NULL, zmsg);
        else
            kvs_name_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.flush")) {
        if (type == ZMSG_REQUEST)
            kvs_flush_request (p, NULL, zmsg);
        else
            kvs_flush_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.commit")) {
        if (type == ZMSG_REQUEST)
            kvs_commit_request (p, NULL, zmsg);
        else
            kvs_commit_response (p, zmsg);
    }
    if (arg)
        free (arg);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    if (!plugin_treeroot (p))
        zsocket_set_subscribe (p->zs_evin, "event.kvs.setroot.");
    zsocket_set_subscribe (p->zs_evin, "event.kvs.debug.");
    if (!(ctx->store = zhash_new ()))
        oom ();
    if (!(ctx->commits = zhash_new ()))
        oom ();
    if (!(ctx->watchlist = zlist_new ()))
        oom ();
    if (plugin_treeroot (p)) {
        if (!(ctx->master.namequeue = zlist_new ()))
            oom ();
    } else {
        if (!(ctx->slave.writeback = zlist_new ()))
            oom ();
        ctx->slave.writeback_state = WB_CLEAN;
    }

    if (plugin_treeroot (p)) {
        json_object *rootdir = util_json_object_new_object ();
        href_t href;

        store (p, rootdir, false, href);
        setroot (p, 0, href);
    } else {
        int seq;
        href_t href;
        json_object *rep = plugin_request (p, NULL, "kvs.getroot");
        const char *rootref = json_object_get_string (rep);

        if (!decode_rootref (rootref, &seq, href))
            msg_exit ("malformed kvs.getroot reply: %s", rootref);
        setroot (p, seq, href);
    }
}

static void kvs_fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    if (ctx->store)
        zhash_destroy (&ctx->store);
    if (ctx->commits)
        zhash_destroy (&ctx->commits);
    if (ctx->watchlist)
        zlist_destroy (&ctx->watchlist);
    if (ctx->slave.writeback)
        zlist_destroy (&ctx->slave.writeback);
    if (ctx->master.namequeue)
        zlist_destroy (&ctx->master.namequeue);
    free (ctx);
}

struct plugin_struct kvssrv = {
    .name      = "kvs",
    .initFn    = kvs_init,
    .finiFn    = kvs_fini,
    .recvFn    = kvs_recv,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
