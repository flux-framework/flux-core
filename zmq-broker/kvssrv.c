/* kvssrv.c - distributed key-value store based on hash tree */

/* JSON directory object: 
 * list of key-value pairs where key is a name, value is a dirent
 *
 * JSON dirent objects:
 * object containing one key-value pair where key is one of
 * "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", and value is a SHA1
 * hash key into ctx->store (FILEREF, DIRREF), or an actual directory
 * or file (value) JSON object (FILEVAL, DIRVAL).  The value types are 
 * only used when a deep copy of a directory is made, for example
 * temporarily during a commit, or to prepare a kvs_get_dir result.
 * They do not appear in "stored" directories (in ctx->store).
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
 *  "c":{"DIRREF",{"d":{"FILEVAL":"baz"}}}}
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

typedef void (*wait_fun_t) (plugin_ctx_t *p, zmsg_t **zmsg);
typedef struct {
    plugin_ctx_t *p;
    int usecount;
    wait_fun_t fun;    
    zmsg_t *zmsg;
} wait_t;

typedef struct {
    json_object *o;
    zlist_t *waitlist;
} hobj_t;

typedef enum { OP_NAME, OP_STORE, OP_FLUSH } optype_t;
typedef struct {
    optype_t type;
    char *key;
    char *ref;
    zmsg_t *zmsg;
} op_t;

typedef struct {
    bool done;
    int rootseq;
    href_t rootdir;
    zlist_t *waitlist;
} commit_t;

typedef struct {
    zhash_t *store;
    href_t rootdir;
    int rootseq;
    zlist_t *writeback;
    enum { WB_CLEAN, WB_FLUSHING, WB_DIRTY } writeback_state;
    zlist_t *commit_ops;
    zhash_t *commits;
} ctx_t;

static void event_kvs_setroot_send (plugin_ctx_t *p);

static wait_t *wait_create (plugin_ctx_t *p, wait_fun_t fun, zmsg_t **zmsg)
{
    wait_t *wp = xzmalloc (sizeof (*wp));
    wp->usecount = 0;
    wp->fun = fun;
    wp->zmsg = *zmsg;
    *zmsg = NULL;
    wp->p = p;

    return wp;
}

static void wait_destroy (wait_t *wp, zmsg_t **zmsg)
{
    assert (zmsg != NULL || wp->zmsg == NULL);
    if (zmsg)
        *zmsg = wp->zmsg;
    free (wp);
}

static void wait_add (zlist_t *waitlist, wait_t *wp)
{
    if (zlist_append (waitlist, wp) < 0)
        oom ();
    wp->usecount++;
}

static bool wait_del (zlist_t *waitlist)
{
    wait_t *wp = zlist_pop (waitlist);
    if (!wp)
        return false;
    if (--wp->usecount == 0) {
        wp->fun (wp->p, &wp->zmsg);
        wait_destroy (wp, NULL);
    }
    return true;
}

static json_object *dirent_create (char *type, void *arg)
{
    json_object *o = util_json_object_new_object ();
    bool valid_type = false;

    if (!strcmp (type, "FILEREF") || !strcmp (type, "DIRREF")) {
        char *ref = arg;

        util_json_object_add_string (o, type, ref);
        valid_type = true;
    } else if (!strcmp (type, "FILEVAL") || !strcmp (type, "DIRVAL")) {
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

static op_t *op_create (optype_t type, char *key, char *ref, zmsg_t *zmsg)
{
    op_t *op = xzmalloc (sizeof (*op));

    op->type = type;
    op->key = key;
    op->ref = ref;
    op->zmsg = zmsg;

    return op;
}

static void op_destroy (op_t *op)
{
    if (op->key)
        free (op->key);
    if (op->ref)
        free (op->ref);
    if (op->zmsg)
        zmsg_destroy (&op->zmsg);
    free (op);
}

static bool op_match (op_t *op1, op_t *op2)
{
    bool match = false;

    if (op1->type == op2->type) {
        switch (op1->type) {
            case OP_STORE:
                if (!strcmp (op1->ref, op2->ref))
                    match = true;
                break;
            case OP_NAME:
                if (!strcmp (op1->key, op2->key))
                    match = true;
                break;
            case OP_FLUSH:
                break;
        }
    }
    return match;
}

static void commit_add_name (plugin_ctx_t *p, const char *key, const char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (plugin_treeroot (p));

    op = op_create (OP_NAME, xstrdup (key), ref ? xstrdup (ref) : NULL, NULL);
    if (zlist_append (ctx->commit_ops, op) < 0)
        oom ();
}

static void writeback_add_name (plugin_ctx_t *p,
                                const char *key, const char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_NAME, xstrdup (key), ref ? xstrdup (ref) : NULL, NULL);
    if (zlist_append (ctx->writeback, op) < 0)
        oom ();
    ctx->writeback_state = WB_DIRTY;
}

static void writeback_add_store (plugin_ctx_t *p, const char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_STORE, NULL, xstrdup (ref), NULL);
    if (zlist_append (ctx->writeback, op) < 0)
        oom ();
    ctx->writeback_state = WB_DIRTY;
}

static void writeback_add_flush (plugin_ctx_t *p, zmsg_t *zmsg)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_FLUSH, NULL, NULL, zmsg);
    if (zlist_append (ctx->writeback, op) < 0)
        oom ();
}

static void writeback_del (plugin_ctx_t *p, optype_t type,
                           char *key, char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t mop = { .type = type, .key = key, .ref = ref, .zmsg = NULL };
    op_t *op = zlist_first (ctx->writeback);

    while (op) {
        if (op_match (op, &mop))
            break;
        op = zlist_next (ctx->writeback);
    }
    if (op) {
        zlist_remove (ctx->writeback, op);
        op_destroy (op);
        /* handle flush(es) now at head of queue */
        while ((op = zlist_head (ctx->writeback)) && op->type == OP_FLUSH) {
            ctx->writeback_state = WB_FLUSHING;
            plugin_send_request_raw (p, &op->zmsg); /* fwd upstream */
            zlist_remove (ctx->writeback, op);
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
            while (wait_del (hp->waitlist))
                ;
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

static bool readahead_dir (plugin_ctx_t *p, json_object *dir, wait_t *wp)
{
    json_object *val;
    json_object_iter iter;
    const char *ref;
    bool done = true;

    json_object_object_foreachC (dir, iter) {
        if (util_json_object_get_string (iter.val, "FILEREF", &ref) == 0) {
            if (!load (p, ref, wp, &val)) {
                done = false;
                continue;
            }
        } else if (util_json_object_get_string (iter.val, "DIRREF", &ref) == 0){
            if (!load (p, ref, wp, &val)) {
                done = false;
                continue;
            }
            if (!readahead_dir (p, val, wp))
                done = false;
        }
    }
    return done;
}

/* If 'nf' is true, only make deep copies of directories, not file content.
 */
static json_object *deep_copy (plugin_ctx_t *p, json_object *dir, wait_t *wp,
                               bool nf)
{
    json_object *dcpy, *val;
    json_object_iter iter;
    const char *ref;

    if (!(dcpy = json_object_new_object ()))
        oom ();
    
    json_object_object_foreachC (dir, iter) {
        if (!nf && util_json_object_get_string (iter.val, "FILEREF", &ref)==0) {
            if (!load (p, ref, wp, &val))
                goto stall;
            json_object_object_add (dcpy, iter.key,
                                    dirent_create ("FILEVAL", val));
        } else if (util_json_object_get_string (iter.val, "DIRREF", &ref)==0) {
            if (!load (p, ref, wp, &val))
                goto stall;
            if (!(val = deep_copy (p, val, wp, nf)))
                goto stall;
            json_object_object_add (dcpy, iter.key,
                                    dirent_create ("DIRVAL", val));
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


static void name_request_send (plugin_ctx_t *p, char *key, const href_t ref)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, key, ref);
    plugin_send_request (p, o, "kvs.name");
    json_object_put (o);
}

static void name (plugin_ctx_t *p, char *key, const href_t ref, bool writeback)
{
    if (writeback) {
        writeback_add_name (p, key, ref);
        name_request_send (p, key, ref);
    } else {
        commit_add_name (p, key, ref);
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
        } else if ((o = json_object_object_get (iter.val, "FILEVAL"))) {
            json_object_get (o);
            (void)store (p, o, false, nhref);
            json_object_object_add (cpy, iter.key,
                                    dirent_create ("FILEREF", nhref));
        } else { /* FILEREF, DIRREF */
            json_object_get (iter.val);
            json_object_object_add (cpy, iter.key, iter.val);
        }
    }
    store (p, cpy, false, href);
}

/* Put (path,ref) to deep copy of root directory.
 *  (assumes deep copy was created with nf=true)
 */
static void deep_put (json_object *dir, const char *path, char *ref)
{
    char *cpy = xstrdup (path);
    char *next, *name = cpy;
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
    json_object_object_del (dir, name);
    if (ref)
        json_object_object_add (dir, name, dirent_create ("FILEREF", ref));
    free (cpy);
}

static void commit (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    op_t *op;
    json_object *dir, *cpy;

    assert (plugin_treeroot (p));

    (void)load (p, ctx->rootdir, NULL, &dir);
    assert (dir != NULL);
    cpy = deep_copy (p, dir, NULL, true);
    assert (cpy != NULL);
    while ((op = zlist_pop (ctx->commit_ops))) {
        assert (op->type == OP_NAME);
        deep_put (cpy, op->key, op->ref);
        op_destroy (op);
    }
    deep_unwind (p, cpy, ctx->rootdir);
    json_object_put (cpy);
    ctx->rootseq++;
}

static void kvs_load (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *val, *cpy = NULL, *o = NULL;
    json_object_iter iter;
    wait_t *wp = NULL;
    bool stall = false;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_load, zmsg);
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

static void kvs_store (plugin_ctx_t *p, zmsg_t **zmsg)
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
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        writeback_del (p, OP_STORE, NULL, iter.key);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_clean (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int s1, s2;
    int rc = 0;
    json_object *rootdir, *cpy;
    href_t href;

    if (zlist_size (ctx->writeback) > 0 || zlist_size (ctx->commit_ops) > 0) {
        plugin_log (p, LOG_ALERT, "cache is busy");
        rc = EAGAIN;
        goto done;
    }
    s1 = zhash_size (ctx->store);
    if (plugin_treeroot (p)) {
        (void)load (p, ctx->rootdir, NULL, &rootdir);
        assert (rootdir != NULL);
        cpy = deep_copy (p, rootdir, NULL, false);
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

static void kvs_name (plugin_ctx_t *p, zmsg_t **zmsg)
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
        name (p, iter.key, json_object_get_string (iter.val), writeback);
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

static void kvs_name_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        writeback_del (p, OP_NAME, iter.key, NULL);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_flush (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    if (plugin_treeroot (p) || ctx->writeback_state == WB_CLEAN)
        plugin_send_response_errnum (p, zmsg, 0);
    else if (zlist_size (ctx->writeback) == 0) {
        plugin_send_request_raw (p, zmsg); /* fwd upstream */
        ctx->writeback_state = WB_FLUSHING;
    } else {
        writeback_add_flush (p, *zmsg); /* enqueue */
        *zmsg = NULL;
    }
}

static void kvs_flush_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    plugin_send_response_raw (p, zmsg); /* fwd downstream */
    if (ctx->writeback_state == WB_FLUSHING)
        ctx->writeback_state = WB_CLEAN;
}

/* Get dirent containing requested key.
 */
static bool walk (plugin_ctx_t *p, json_object *dir, const char *path,
                  json_object **dp, wait_t *wp)
{
    char *cpy = xstrdup (path);
    char *next, *name = cpy;
    const char *ref;
    json_object *dirent = NULL;

    while ((next = strchr (name, '.'))) {
        *next++ = '\0';
        if (!(dirent = json_object_object_get (dir, name)))
            goto done;
        if (!util_json_object_get_string (dirent, "DIRREF", &ref) < 0) {
            dirent = NULL;
            goto done;
        }
        if (!load (p, ref, wp, &dir))
            goto stall;
        name = next;
    }
    dirent = json_object_object_get (dir, name);
done:
    free (cpy);
    *dp = dirent;    
    return true;
stall:
    free (cpy);
    return false;
}

static bool lookup_file (plugin_ctx_t *p, json_object *root, wait_t *wp,
                         const char *name, json_object **valp)
{
    json_object *dirent, *val = NULL;
    const char *ref;

    if (!strcmp (name, "."))
        goto done;
    if (!walk (p, root, name, &dirent, wp))
        goto stall;
    if (!dirent || util_json_object_get_string (dirent, "FILEREF", &ref) < 0)
        goto done;
    if (!load (p, ref, wp, &val))
        goto stall;
    if (val)
        json_object_get (val);
done:
    *valp = val;
    return true;
stall:
    return false;
}

static bool lookup_dir (plugin_ctx_t *p, json_object *root, wait_t *wp,
                        const char *name, json_object **valp)
{
    json_object *dirent, *dir, *val = NULL;
    const char *ref;

    if (!strcmp (name, "."))
        dir = root;
    else {
        if (!walk (p, root, name, &dirent, wp))
            goto stall;
        if (!dirent || util_json_object_get_string (dirent, "DIRREF", &ref) < 0)
            goto done;
        if (!load (p, ref, wp, &dir))
            goto stall;
    }
    if (!plugin_treeroot (p) && !readahead_dir (p, dir, wp))
        goto stall;
    if (!(val = deep_copy (p, dir, wp, false)))
        goto stall;
done:
    *valp = val;
    return true;
stall:
    return false;
}

static void kvs_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *root, *val, *ocpy = NULL, *o = NULL;
    json_object_iter iter;
    wait_t *wp = NULL;
    bool stall = false;
    bool docache;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    wp = wait_create (p, kvs_get, zmsg);
    if (!load (p, ctx->rootdir, wp, &root)) {
        stall = true;
        goto done;
    }
    ocpy = util_json_object_new_object ();
    json_object_object_foreachC (o, iter) {
        if (util_json_object_get_boolean (iter.val, "cache", &docache) < 0) {
            json_object_object_add (ocpy, iter.key, NULL);
        } else if (docache) {
            if (!lookup_dir (p, root, wp, iter.key, &val))
                stall = true;
        } else {
            if (!lookup_file (p, root, wp, iter.key, &val))
                stall = true;
        }
        if (!stall)
            json_object_object_add (ocpy, iter.key, val);
    }
    if (!stall) {
        wait_destroy (wp, zmsg);
        plugin_send_response (p, zmsg, ocpy);
    }
done:
    if (o)
        json_object_put (o);
    if (ocpy)
        json_object_put (ocpy);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_put (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    json_object_iter iter;
    href_t ref;
    bool writeback = !plugin_treeroot (p);

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    json_object_object_foreachC (o, iter) {
        if (json_object_get_type (iter.val) == json_type_null) {
            name (p, iter.key, NULL, writeback);
        } else { 
            json_object_get (iter.val);
            store (p, iter.val, writeback, ref);
            name (p, iter.key, ref, writeback);
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

static void kvs_commit (plugin_ctx_t *p, zmsg_t **zmsg)
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
    wp = wait_create (p, kvs_commit, zmsg);
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
    while (wait_del (cp->waitlist))
        ;
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void kvs_getroot (plugin_ctx_t *p, zmsg_t **zmsg)
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

static void event_kvs_debug (plugin_ctx_t *p, zmsg_t **zmsg, char *arg)
{
    ctx_t *ctx = p->ctx;

    if (!strcmp (arg, "writeback.size"))
        plugin_log (p, LOG_DEBUG, "writeback %d", zlist_size (ctx->writeback));
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

static void kvs_recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;

    if (cmb_msg_match (*zmsg, "kvs.getroot")) {
        kvs_getroot (p, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.kvs.setroot.", &arg)) {
        event_kvs_setroot (p, arg, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.kvs.debug.", &arg)) {
        event_kvs_debug (p, zmsg, arg);
    } else if (cmb_msg_match (*zmsg, "kvs.clean")) {
        kvs_clean (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.get")) {
        kvs_get (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.put")) {
        kvs_put (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.load")) {
        if (type == ZMSG_REQUEST)
            kvs_load (p, zmsg);
        else
            kvs_load_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.store")) {
        if (type == ZMSG_REQUEST)
            kvs_store (p, zmsg);
        else
            kvs_store_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.name")) {
        if (type == ZMSG_REQUEST)
            kvs_name (p, zmsg);
        else
            kvs_name_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.flush")) {
        if (type == ZMSG_REQUEST)
            kvs_flush (p, zmsg);
        else
            kvs_flush_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.commit")) {
        if (type == ZMSG_REQUEST)
            kvs_commit (p, zmsg);
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
    if (!(ctx->writeback = zlist_new ()))
        oom ();
    ctx->writeback_state = WB_CLEAN;
    if (!(ctx->commit_ops = zlist_new ()))
        oom ();
    if (!(ctx->commits = zhash_new ()))
        oom ();

    if (plugin_treeroot (p)) {
        json_object *rootdir = util_json_object_new_object ();
        href_t href;

        store (p, rootdir, false, href);
        setroot (p, 0, href);
    } else {
        json_object *rep = plugin_request (p, NULL, "kvs.getroot");
        const char *rootref = json_object_get_string (rep);
        int seq;
        href_t href;

        if (!decode_rootref (rootref, &seq, href))
            msg_exit ("malformed kvs.getroot reply: %s", rootref);
        setroot (p, seq, href);
    }
}

static void kvs_fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->store);
    zlist_destroy (&ctx->writeback);
    zlist_destroy (&ctx->commit_ops);
    zhash_destroy (&ctx->commits);
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
