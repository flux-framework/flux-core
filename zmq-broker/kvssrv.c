/* kvssrv.c - key-value store based on hash tree */

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

typedef struct {
    json_object *o;
    zlist_t *reqs;
} hobj_t;

typedef enum { OP_NAME, OP_STORE, OP_FLUSH } optype_t;
typedef struct {
    optype_t type;
    char *key;
    char *ref;
    zmsg_t *flush;
    char *sender;
} op_t;

typedef enum { WB_CLEAN, WB_FLUSHING, WB_DIRTY } wbstate_t;
typedef struct {
    zhash_t *store;
    href_t rootdir;
    zlist_t *writeback;
    wbstate_t writeback_state;
} ctx_t;

static void kvs_load (plugin_ctx_t *p, zmsg_t **zmsg);
static void kvs_get (plugin_ctx_t *p, zmsg_t **zmsg);

static op_t *op_create (optype_t type, char *key, char *ref, zmsg_t *flush)
{
    op_t *op = xzmalloc (sizeof (*op));

    op->type = type;
    op->key = key;
    op->ref = ref;
    op->flush = flush;
    if (flush)
        op->sender = cmb_msg_sender (flush);

    return op;
}

static void op_destroy (op_t *op)
{
    if (op->key)
        free (op->key);
    if (op->ref)
        free (op->ref);
    if (op->flush)
        zmsg_destroy (&op->flush);
    if (op->sender)
        free (op->sender);
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

static void writeback_add_name (plugin_ctx_t *p,
                                const char *key, const char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    op = op_create (OP_NAME, xstrdup (key), ref ? xstrdup (ref) : NULL, NULL);
    if (zlist_append (ctx->writeback, op) < 0)
        oom ();
    if (!plugin_treeroot (p))
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

static void writeback_add_flush (plugin_ctx_t *p, zmsg_t *flush)
{
    ctx_t *ctx = p->ctx;
    op_t *op;

    assert (!plugin_treeroot (p));

    op = op_create (OP_FLUSH, NULL, NULL, flush);
    if (zlist_append (ctx->writeback, op) < 0)
        oom ();
}

static void writeback_del (plugin_ctx_t *p, optype_t type,
                           char *key, char *ref)
{
    ctx_t *ctx = p->ctx;
    op_t mop = { .type = type, .key = key, .ref = ref };
    op_t *op = zlist_first (ctx->writeback);

    while (op) {
        if (op_match (op, &mop))
            break;
        op = zlist_next (ctx->writeback);
    }
    if (op) {
        zlist_remove (ctx->writeback, op);
        op_destroy (op);
    }
    /* If a flush is at the head of the queue, handle it.
     */
    op = zlist_first (ctx->writeback);
    if (op && op->type == OP_FLUSH) {
        op = zlist_pop (ctx->writeback);
        if (ctx->writeback_state == WB_CLEAN) {
            plugin_send_response_raw (p, &op->flush); /* respond */
        } else {
            plugin_send_request_raw (p, &op->flush); /* fwd upstream */
            ctx->writeback_state = WB_FLUSHING;
        }
        op_destroy (op);
    }
}

static hobj_t *hobj_create (json_object *o)
{
    hobj_t *hp = xzmalloc (sizeof (*hp));

    hp->o = o;
    if (!(hp->reqs = zlist_new ()))
        oom ();

    return hp;
}

static void hobj_destroy (hobj_t *hp)
{
    if (hp->o)
        json_object_put (hp->o);
    assert (zlist_size (hp->reqs) == 0);
    zlist_destroy (&hp->reqs);
    free (hp);
}

static void load_request_send (plugin_ctx_t *p, const href_t ref)
{
    json_object *o = util_json_object_new_object ();

    json_object_object_add (o, ref, NULL);
    plugin_send_request (p, o, "kvs.load");
    json_object_put (o);
}

static bool load (plugin_ctx_t *p, const href_t ref, zmsg_t **zmsg,
                  json_object **op)
{
    ctx_t *ctx = p->ctx;
    hobj_t *hp = zhash_lookup (ctx->store, ref);
    bool done = true;

    if (plugin_treeroot (p)) {
        if (!hp)
            plugin_panic (p, "dangling ref %s", ref);
    } else {
        if (!hp) {
            hp = hobj_create (NULL);
            zhash_insert (ctx->store, ref, hp);
            zhash_freefn (ctx->store, ref, (zhash_free_fn *)hobj_destroy);
            load_request_send (p, ref);
        }
        if (!hp->o) {
            assert (zmsg != NULL);
            if (zlist_append (hp->reqs, *zmsg) < 0)
                oom ();
            *zmsg = NULL;
            done = false;
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
    zmsg_t *zmsg;

    compute_json_href (o, ref);

    if ((hp = zhash_lookup (ctx->store, ref))) {
        if (hp->o) {
            json_object_put (o);
        } else {
            hp->o = o;
            while ((zmsg = zlist_pop (hp->reqs))) {
                if (cmb_msg_match (zmsg, "kvs.load"))
                    kvs_load (p, &zmsg);
                else if (cmb_msg_match (zmsg, "kvs.get"))
                    kvs_get (p, &zmsg);
                if (zmsg)
                    zmsg_destroy (&zmsg);
            }
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

static void name_request_send (plugin_ctx_t *p, char *key, const href_t ref)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, key, ref);
    plugin_send_request (p, o, "kvs.name");
    json_object_put (o);
}

static void name (plugin_ctx_t *p, char *key, const href_t ref, bool writeback)
{
    writeback_add_name (p, key, ref);
    if (writeback)
        name_request_send (p, key, ref);
}

static bool setroot (plugin_ctx_t *p, const char *arg)
{
    ctx_t *ctx = p->ctx;

    if (!arg || strlen (arg) + 1 != sizeof (href_t))
        return false;
    memcpy (ctx->rootdir, arg, sizeof (href_t));
    return true;
}

static void commit (plugin_ctx_t *p, const char *name)
{
    ctx_t *ctx = p->ctx;
    op_t *op;
    json_object *o, *cpy;
    char *commit_name;

    assert (plugin_treeroot (p));

    (void)load (p, ctx->rootdir, NULL, &o);
    assert (o != NULL);
    cpy = util_json_object_dup (o);
    while ((op = zlist_pop (ctx->writeback))) {
        switch (op->type) {
            case OP_NAME:
                if (op->ref)
                    util_json_object_add_string (cpy, op->key, op->ref);
                else
                    json_object_object_del (cpy, op->key);
                break;
            case OP_STORE: /* shouldn't be any at treeroot */
            case OP_FLUSH: /* shouldn't be any at treeroot */
                break;
        }
        op_destroy (op);
    }
    if (asprintf (&commit_name, "commit.%s", name) < 0)
        oom ();
    util_json_object_add_string (cpy, commit_name, ctx->rootdir);
    store (p, cpy, false, ctx->rootdir);
    plugin_send_event (p, "event.kvs.setroot.%s", ctx->rootdir);
    free (commit_name);
}

static void kvs_load (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *val, *cpy = NULL, *o = NULL;
    json_object_iter iter;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_dup (o);
    json_object_object_foreachC (o, iter) {
        if (!load (p, iter.key, zmsg, &val))
            goto done; /* stall */
        json_object_get (val);
        json_object_object_add (cpy, iter.key, val);
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
    cpy = util_json_object_dup (o);
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

static void kvs_name (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *cpy = NULL, *o = NULL;
    json_object_iter iter;
    bool writeback = !plugin_treeroot (p);

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    cpy = util_json_object_dup (o);
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

    if (ctx->writeback_state == WB_CLEAN) {
        plugin_send_response_raw (p, zmsg); /* respond */
    } else {
        writeback_add_flush (p, *zmsg); /* enqueue */
        *zmsg = NULL;
    }
}

static void kvs_flush_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    
    if (ctx->writeback_state == WB_FLUSHING)
        ctx->writeback_state = WB_CLEAN;
    plugin_send_response_raw (p, zmsg); /* fwd downstream */
}

static void kvs_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *dir, *val, *cpy = NULL, *o = NULL;
    json_object_iter iter;
    const char *ref;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!load (p, ctx->rootdir, zmsg, &dir))
        goto done; /* stall */
    cpy = util_json_object_dup (o);
    json_object_object_foreachC (o, iter) {
        if (util_json_object_get_string (dir, iter.key, &ref) < 0)
            continue;
        if (!load (p, ref, zmsg, &val))
            goto done; /* stall */
        json_object_get (val);
        json_object_object_add (cpy, iter.key, val); 
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

static void kvs_commit (plugin_ctx_t *p, zmsg_t **zmsg)
{
    //ctx_t *ctx = p->ctx;
    json_object *o;
    const char *name;
    bool active;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0
            || util_json_object_get_boolean (o, "active", &active) < 0) {
        plugin_log (p, LOG_ERR, "%s: bad message", __FUNCTION__);
        //goto done;
        return;
    }
    if (active) {
        if (plugin_treeroot (p))
            commit (p, name);
        else {
            //zmsg_t *cpy = zmsg_dup (*zmsg);

            //plugin_send_request (p, cpy);
        }
    }

    plugin_send_response_errnum (p, zmsg, 0); /* success */
}

static void kvs_getroot (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o;

    if (!(o = json_object_new_string (ctx->rootdir)))
        oom ();
    plugin_send_response (p, zmsg, o);
}

static void event_kvs_setroot (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    assert (!plugin_treeroot (p));
    if (!setroot (p, arg))
        plugin_log (p, LOG_ERR, "%s: malformed rootref %s", __FUNCTION__, arg);
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
    } else if (cmb_msg_match (*zmsg, "kvs.disconnect")) {
        //kvs_disconnect (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.get")) {
        kvs_get (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.put")) {
        kvs_put (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "kvs.commit")) {
        kvs_commit (p, zmsg);
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
        zsocket_set_subscribe (p->zs_evin, "event.kvs.");
    if (!(ctx->store = zhash_new ()))
        oom ();
    if (!(ctx->writeback = zlist_new ()))
        oom ();
    ctx->writeback_state = WB_CLEAN;

    if (plugin_treeroot (p)) {
        json_object *rootdir = util_json_object_new_object ();

        store (p, rootdir, false, ctx->rootdir);
    } else {
        json_object *rep = plugin_request (p, NULL, "kvs.getroot");

        if (!setroot (p, json_object_get_string (rep)))
            plugin_panic (p, "malformed kvs.getroot reply");
    }
}

static void kvs_fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->store);
    zlist_destroy (&ctx->writeback);
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
