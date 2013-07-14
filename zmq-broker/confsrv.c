/* confsrv.c - yet another key value store */

/* Master copy of store is at the tree root.  Other nodes have caches
 * populated on demand on a key by key basis.  The store has a monotonically
 * increasing integer version number.
 *
 * Put and commit requests are forwarded to the root.
 * Puts are applied against a new copy of the store which becomes the
 * current store after a commit.  An "event.conf.update.<rev>" message
 * is published when the master store is updated.  Caches of older <rev>
 * numbers are invaldated in response to this event.
 *
 * Gets are forwarded up the tree until they can be satisfied.  Multiple
 * requests for the same key are aggregated behind one upstream request.
 * When a result is returned from upstream, it is entered into the local
 * cache.
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

#include "zmq.h"
#include "cmb.h"
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "confsrv.h"

typedef struct {
    zlist_t *reply_to;  /* item = (zmsg_t *) */
    char *val;
    bool val_initialized;
} req_t;

typedef struct {
    zhash_t *store;
    zhash_t *store_next;
    int store_version;
    zhash_t *proxy;     /* proxy{key} = (req_t *) */
    zhash_t *watcher;   /* watcher{key} = (req_t *) */
} ctx_t;

static void _req_destroy (req_t *req)
{
    if (req->val)
        free (req->val);
    assert (zlist_size (req->reply_to) == 0);
    zlist_destroy (&req->reply_to);
}

static req_t *_req_create (void)
{
    req_t *req = xzmalloc (sizeof (req_t));

    if (!(req->reply_to = zlist_new ()))
        oom ();
    return req;
}

static req_t *_install_watcher (plugin_ctx_t *p, const char *key, zmsg_t *zmsg)
{
    ctx_t *ctx = p->ctx;
    req_t *wp;
    zmsg_t *cpy = zmsg_dup (zmsg);

    if (!(wp = zhash_lookup (ctx->watcher, key))) {
        wp = _req_create ();
        zhash_update (ctx->watcher, key, wp);
        zhash_freefn (ctx->watcher, key, (zhash_free_fn *)_req_destroy);
    }
    zlist_append (wp->reply_to, cpy);
    return wp;
}

static void _send_watcher_responses (plugin_ctx_t *p, req_t *wp,
                                     json_object *vo, int store_version)
{
    zmsg_t *zmsg = zlist_first (wp->reply_to);

    while (zmsg) {
        zmsg_t *cpy = zmsg_dup (zmsg);
        json_object *o;

        if (cmb_msg_decode (cpy, NULL, &o) == 0) {
            if (o) {
                util_json_object_add_int (o, "store_version", store_version);
                json_object_get (vo);
                json_object_object_add (o, "val", vo);
                plugin_send_response (p, &cpy, o);
                json_object_put (o);
            }
        }
        if (cpy)
            zmsg_destroy (&cpy);
        zmsg = zlist_next (wp->reply_to);
    }
}

static bool _valcmp (const char *v1, const char *v2)
{
    return (v1 == v2 || (v1 && v2 && !strcmp (v1, v2)));
}

static void _update_watcher (plugin_ctx_t *p, req_t *wp, const char *val,
                            int store_version)
{
    json_object *vo = NULL;

    if (!wp->val_initialized) {
        wp->val = val ? xstrdup (val) : NULL;
        wp->val_initialized = true;
    } else if (!_valcmp( wp->val, val)) {
        if (wp->val)
            free (wp->val);
        wp->val = val ? xstrdup (val) : NULL;
        if (val)
            vo = json_tokener_parse (val);
        _send_watcher_responses (p, wp, vo, store_version);
        if (vo)
            json_object_put (vo);
    }
}

static void _update_version (plugin_ctx_t *p, int new_version)
{
    ctx_t *ctx = p->ctx;
    zlist_t *keys;
    char *key;

    assert (!plugin_treeroot (p));

    zhash_destroy (&ctx->store);
    if (!(ctx->store = zhash_new ()))
        oom ();
    ctx->store_version = new_version;

    /* Request values for watched keys.
     * Watchers will be updated as needed when the replies come in.
     */
    if (!(keys = zhash_keys (ctx->watcher)))
        oom ();
    while ((key = zlist_pop (keys))) {
        json_object *o = util_json_object_new_object ();
        util_json_object_add_string (o, "key", key);
        util_json_object_add_boolean (o, "watch", false);
        plugin_send_request (p, o, "conf.get");
        json_object_put (o);
    }
    zlist_destroy (&keys);
}

/* "conf.get" request received.
 * Answer or initiate upstream proxy request to instiate item in cache.
 */
static void _conf_get (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *vo, *o = NULL;
    const char *key, *val;
    req_t *req, *wp = NULL;
    bool watch;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "key", &key) < 0
            || util_json_object_get_boolean (o, "watch", &watch) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }

    if (watch)
        wp = _install_watcher (p, key, *zmsg);

    /* Found in local cache.  Respond with value.
     */
    if ((val = zhash_lookup (ctx->store, key)) != NULL) {
        if (!(vo = json_tokener_parse (val)))
            msg_exit ("conf: JSON parse error %s=%s", key, val);
        json_object_object_add (o, "val", vo);
        util_json_object_add_int (o, "store_version", ctx->store_version);
        plugin_send_response (p, zmsg, o);
        if (wp)
            _update_watcher (p, wp, val, ctx->store_version);

    /* Not found in local cache and we hold master copy.
     * Respond with value missing (indicating key not set).
     */
    } else if (plugin_treeroot (p)) {
        json_object_object_add (o, "val", NULL);
        util_json_object_add_int (o, "store_version", ctx->store_version);
        plugin_send_response (p, zmsg, o);
        if (wp)
            _update_watcher (p, wp, val, ctx->store_version);

    /* Not the master, and we have already sent a proxy request upstream
     * for this key.  Add this request to the reply-to list for the proxy.
     */
    } else if ((req = zhash_lookup (ctx->proxy, key))) {
        zlist_append (req->reply_to, *zmsg);
        *zmsg = NULL; /* now owned by req->reply_to */

    /* Not the master, no proxy request in progress.
     * Initiate a proxy request with this request in the reply-to list.
     */
    } else {
        req = _req_create ();
        zhash_update (ctx->proxy, key, req);
        zhash_freefn (ctx->proxy, key, (zhash_free_fn *)_req_destroy);
        util_json_object_add_boolean (o, "watch", false);
        plugin_send_request (p, o, "conf.get");
        zlist_append (req->reply_to, *zmsg);
        *zmsg = NULL; /* now owned by req->reply_to */
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _send_proxy_responses (plugin_ctx_t *p, req_t *req,
                                   json_object *vo, int store_version)
{
    zmsg_t *zmsg;

    while ((zmsg = zlist_pop (req->reply_to))) {
        json_object *o = NULL;
        if (cmb_msg_decode (zmsg, NULL, &o) == 0) {
            if (o) {
                util_json_object_add_int (o, "store_version", store_version);
                json_object_get (vo);
                json_object_object_add (o, "val", vo);
                plugin_send_response (p, &zmsg, o);
                json_object_put (o);
            }
        }
        if (zmsg)
            zmsg_destroy (&zmsg);
    }
}

/* "conf.get" proxy response received.
 * Update cache and respond to original requests.
 */
static void _conf_get_response (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *vo, *o = NULL;
    const char *key, *val = NULL;
    int store_version;
    req_t *req, *wp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
          || util_json_object_get_string (o, "key", &key) < 0
          || util_json_object_get_int (o, "store_version", &store_version) < 0)
        goto done;
    /* N.B.  If response is newer than our cache, update now
     * so we can store the result.
     */
    if (store_version > ctx->store_version)
        _update_version (p, store_version);
    /* If value is not set, we do not update the cache.
     * We are not caching "negative" lookups (yet).
     */
    if ((vo = json_object_object_get (o, "val"))) {
        val = json_object_to_json_string (vo);
        zhash_update (ctx->store, key, xstrdup (val));
        zhash_freefn (ctx->store, key, (zhash_free_fn *)free);
    }
    /* Now respond to the original requests.
     */
    if ((req = zhash_lookup (ctx->proxy, key))) {
        _send_proxy_responses (p, req, vo, store_version);
        zhash_delete (ctx->proxy, key);
    }
    /* And update watchers for this key.
     */
    if ((wp = zhash_lookup (ctx->watcher, key)))
        _update_watcher (p, wp, val, ctx->store_version);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg); 
}

static void _delete_sender_from_req (req_t *req, char *sender)
{
    zlist_t *cpy;
    zmsg_t *z;

    if (!(cpy = zlist_new ()))
        oom ();
    while ((z = zlist_pop (req->reply_to))) {
        char *sender2 = cmb_msg_sender (z);
        if (sender && sender2 && !strcmp (sender, sender2))
            zmsg_destroy (&z);
        else
            zlist_append (cpy, z);
        if (sender2)
            free (sender2);
    }
    zlist_destroy (&req->reply_to);
    req->reply_to = cpy;
}

static void _delete_sender_from_reqhash (zhash_t *h, char *sender)
{
    zlist_t *keys;
    const char *key;

    if (!(keys = zhash_keys (h)))
        oom ();
    while ((key = zlist_pop (keys)))
        _delete_sender_from_req (zhash_lookup (h, key), sender);
    zlist_destroy (&keys);
}

static void _conf_disconnect (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    char *sender = cmb_msg_sender (*zmsg);

    _delete_sender_from_reqhash (ctx->watcher, sender);
    _delete_sender_from_reqhash (ctx->proxy, sender);
    if (sender)
        free (sender);
    zmsg_destroy (zmsg);
}

static void _conf_put (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *vo, *o = NULL;
    const char *key, *val;

    assert (plugin_treeroot (p));
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "key", &key) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (!ctx->store_next && !(ctx->store_next = zhash_dup (ctx->store)))
        oom ();
    if ((vo = json_object_object_get (o, "val"))) {
        val = json_object_to_json_string (vo);
        zhash_update (ctx->store_next, key, xstrdup (val));
        zhash_freefn (ctx->store_next, key, (zhash_free_fn *) free);
    } else {
        zhash_delete (ctx->store_next, key);
    }
    plugin_send_response_errnum (p, zmsg, 0); /* success */
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _conf_commit (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    zlist_t *keys;
    const char *key;

    assert (plugin_treeroot (p));
    if (ctx->store_next) {
        zhash_destroy (&ctx->store);
        ctx->store = ctx->store_next;
        ctx->store_next = NULL;
    }
    ctx->store_version++;
    plugin_send_event (p, "event.conf.update.%d", ctx->store_version);
        plugin_send_response_errnum (p, zmsg, 0);

    if (!(keys = zhash_keys (ctx->watcher)))
        oom ();
    while ((key = zlist_pop (keys)))
        _update_watcher (p, zhash_lookup (ctx->watcher, key),
                            zhash_lookup (ctx->store, key), ctx->store_version);
    zlist_destroy (&keys);
}

typedef struct {
    plugin_ctx_t *p;
    zmsg_t *zmsg;
} conf_list_one_arg_t;

static int _conf_list_one (const char *key, void *item, void *arg)
{
    conf_list_one_arg_t *la = arg;
    json_object *vo, *o = util_json_object_new_object ();
    ctx_t *ctx = la->p->ctx;
    zmsg_t *cpy;

    if (!(cpy = zmsg_dup (la->zmsg)))
        oom ();
    util_json_object_add_int (o, "store_version", ctx->store_version);
    util_json_object_add_string (o, "key", key);
    if (!(vo = json_tokener_parse ((char *)item)))
        msg_exit ("conf: JSON parse error %s=%s", key, (char *)item);
    json_object_object_add (o, "val", vo);
    plugin_send_response (la->p, &cpy, o);
    json_object_put (o);
    return 0;
}

static void _conf_list (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    conf_list_one_arg_t la = { .p = p, .zmsg = *zmsg };

    assert (plugin_treeroot (p));
    zhash_foreach (ctx->store, _conf_list_one, &la);
    plugin_send_response_errnum (p, zmsg, 0); /* EOF */
}

static void _event_conf_update (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int new_version = strtoul (arg, NULL, 10);

    assert (!plugin_treeroot (p));
    if (new_version > ctx->store_version)
        _update_version (p, new_version);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;

    if (cmb_msg_match (*zmsg, "conf.get")) {
        if (type == ZMSG_REQUEST)
            _conf_get (p, zmsg);
        else
            _conf_get_response (p, zmsg);
    } else if (cmb_msg_match (*zmsg, "conf.put")) {
        if (type == ZMSG_REQUEST) {
            if (plugin_treeroot (p))
                _conf_put (p, zmsg);
            else
                plugin_send_request_raw (p, zmsg); /* fwd to root */
        } else
            plugin_send_response_raw (p, zmsg); /* fwd to requestor */
    } else if (cmb_msg_match (*zmsg, "conf.commit")) {
        if (type == ZMSG_REQUEST) {
            if (plugin_treeroot (p))
                _conf_commit (p, zmsg);
            else
                plugin_send_request_raw (p, zmsg); /* fwd to root */
        } else
            plugin_send_response_raw (p, zmsg); /* fwd to requestor */
    } else if (cmb_msg_match (*zmsg, "conf.list")) {
        if (type == ZMSG_REQUEST) {
            if (plugin_treeroot (p))
                _conf_list (p, zmsg);
            else
                plugin_send_request_raw (p, zmsg); /* fwd to root */
        } else
            plugin_send_response_raw (p, zmsg); /* fwd to requetsor */
    } else if (cmb_msg_match (*zmsg, "conf.disconnect")) {
        _conf_disconnect (p, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.conf.update.", &arg)) {
        _event_conf_update (p, arg, zmsg);
    }

    if (arg)
        free (arg);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    if (!plugin_treeroot (p))
        zsocket_set_subscribe (p->zs_evin, "event.conf.");
    if (!(ctx->store = zhash_new ()))
        oom ();
    if (!(ctx->proxy = zhash_new ()))
        oom ();
    if (!(ctx->watcher = zhash_new ()))
        oom ();
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zhash_destroy (&ctx->store);
    if (ctx->store_next)
        zhash_destroy (&ctx->store_next);
    zhash_destroy (&ctx->proxy);
    zhash_destroy (&ctx->watcher);
    free (ctx);
}

struct plugin_struct confsrv = {
    .name      = "conf",
    .initFn    = _init,
    .finiFn    = _fini,
    .recvFn    = _recv,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
