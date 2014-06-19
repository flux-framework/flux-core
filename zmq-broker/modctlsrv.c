/* modctlsrv.c - bulk module loading */

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
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "util.h"
#include "log.h"
#include "plugin.h"
#include "shortjson.h"
#include "reduce.h"

typedef struct {
    flux_t h;
    zhash_t *modules;
    char *tmpdir;
    red_t r;
    bool master;
} ctx_t;

static const int red_timeout_msec_master = 20;
static const int red_timeout_msec = 2;

static void modctl_reduce (flux_t h, zlist_t *items, void *arg);
static void modctl_sink (flux_t h, void *item, void *arg);

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->modules);
    (void)rmdir (ctx->tmpdir);
    flux_red_destroy (ctx->r);
    free (ctx->tmpdir);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "modctlsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->modules = zhash_new ()))
            oom ();
        ctx->h = h;
        ctx->master = flux_treeroot (h);
        if (asprintf (&ctx->tmpdir, "/tmp/flux-modctl.XXXXXX") < 0)
            oom ();
        if (!mkdtemp (ctx->tmpdir))
            err_exit ("mkdtemp");
        ctx->r = flux_red_create (h, modctl_sink, modctl_reduce,
                                  FLUX_RED_TIMEDFLUSH, ctx);
        flux_red_set_timeout_msec (ctx->r, ctx->master ? red_timeout_msec_master
                                                       : red_timeout_msec);
        flux_aux_set (h, "modctlsrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

/* FIXME: use hostlist */
char *nl_merge (const char *a, const char *b)
{
    char *s;
    if (asprintf (&s, "%s,%s", a, b) < 0)
        oom ();
    return s;
}

static void modctl_reduce (flux_t h, zlist_t *items, void *arg)
{
    //ctx_t *ctx = arg;
    JSON o, a;
    json_object_iter iter;
    JSON mods, amods = NULL;
    int seq, aseq = -1;

    o = zlist_first (items);
    while (o) {
        /* Ignore malformed and old requests.
         */
        if (!Jget_int (o, "seq", &seq) || seq < aseq
                                       || !Jget_obj (o, "mods", &mods))
            goto next;
        /* If request is newer than aggregate, dump old and start over.
         */
        if (seq > aseq) {
            if (amods)
                Jput (amods);
            amods = Jget (mods);
            aseq = seq;
            goto next;
        }
        /* Walk through list of loaded modules, accumulating a union list
         * in the aggregate, and combining nodelists.
         * FIXME: ignoring mismatched size/digest.
         */
        json_object_object_foreachC (mods, iter) {
            JSON amod;
            if (!Jget_obj (amods, iter.key, &amod)) {
                Jadd_obj (amods, iter.key, iter.val);
            } else {
                const char *nl = "", *anl = "";
                (void)Jget_str (iter.val, "nodelist", &nl);
                (void)Jget_str (amod, "nodelist", &anl);
                char *s = nl_merge (anl, nl);
                json_object_object_del (amod, "nodelist");
                Jadd_str (amod, "nodelist", s);
                free (s);
            }
        }
next:
        o = zlist_next (items);
    }
    while ((o = zlist_pop (items)))
        Jput (o);
    a = Jnew ();
    Jadd_int (a, "seq", aseq);
    Jadd_obj (a, "mods", amods);
    Jput (amods);
    zlist_append (items, a);
}

static void modctl_sink (flux_t h, void *item, void *arg)
{
    ctx_t *ctx = arg;
    JSON o = item;

    if (!ctx->master)
        flux_request_send (h, o, "modctl.push");
    Jput (o);
}

static int push_request (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    int seq;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
            || !Jget_int (request, "seq", &seq)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    flux_red_append (ctx->r, Jget (request), seq);
done:
    Jput (request);
    return 0;
}

static int write_all (int fd, uint8_t *buf, int len)
{
    int n, count = 0;
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
}

static void freetemp (char *path)
{
    (void)unlink (path);
    free (path);
}

/* Install module out of KVS.
 */
static void installmod (ctx_t *ctx, const char *name)
{
    char *key = NULL;
    char *path = NULL;
    JSON mod = NULL, args;
    uint8_t *buf = NULL;
    int fd, len;

    if (asprintf (&key, "conf.modctl.modules.%s", name) < 0)
        oom ();
    if (kvs_get (ctx->h, key, &mod) < 0 || !Jget_obj (mod, "args", &args)
            || util_json_object_get_data (mod, "data", &buf, &len) < 0)
        goto done; /* kvs/parse error */
    if (asprintf (&path, "%s/%s.so", ctx->tmpdir, name) < 0)
        oom ();
    if ((fd = open (path, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0)
        err_exit ("%s", path);
    if (write_all (fd, buf, len) < 0)
        err_exit ("%s", path);
    if (close (fd) < 0)
        err_exit ("%s", path);
    if (flux_insmod (ctx->h, -1, path, FLUX_MOD_FLAGS_MANAGED, args) < 0) {
        flux_log (ctx->h, LOG_ERR, "flux_insmod %s", name);
        freetemp (path);
    } else {
        zhash_update (ctx->modules, name, path);
        zhash_freefn (ctx->modules, name, (zhash_free_fn *)freetemp);
    }
done:
    if (key)
        free (key);
    if (buf)
        free (buf);
    Jput (mod);
}

static void conf_cb (const char *path, int seq, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    kvsitr_t itr;
    JSON mod;
    const char *name;
    zlist_t *keys;
    char *key;
    kvsdir_t dir;

    if (errnum != 0)
        seq = 0;
    /* Install managed modules listed in kvs.
     */
    if (kvs_get_dir (ctx->h, &dir, "conf.modctl.modules") == 0) {
        if (!(itr = kvsitr_create (dir)))
            oom ();
        while ((name = kvsitr_next (itr))) {
            if (!zhash_lookup (ctx->modules, name))
                installmod (ctx, name);
        }
        kvsitr_destroy (itr);
    }

    /* Remove managed modules not listed in kvs.
     */
    if (!(keys = zhash_keys (ctx->modules)))
        oom ();
    name = zlist_first (keys);
    while (name) {
        if (asprintf (&key, "conf.modctl.modules.%s", name) < 0)
            oom ();
        if (kvs_get (ctx->h, key, &mod) < 0) {
            if (flux_rmmod (ctx->h, -1, name, FLUX_MOD_FLAGS_MANAGED) < 0)
                flux_log (ctx->h, LOG_ERR, "flux_rmmod %s", name);
            zhash_delete (ctx->modules, name);
        } else {
            Jput (mod);
        }
        free (key);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);

    /* Reduce module list into KVS.
     */
    JSON mods;
    if ((mods = flux_lsmod (ctx->h, -1))) {
        JSON o = Jnew ();
        Jadd_int (o, "seq", seq);
        Jadd_obj (o, "mods", mods);
        flux_red_append (ctx->r, o, seq);
        Jput (mods);
    }
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST, "modctl.push",              push_request },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_int (h, "conf.modctl.seq", conf_cb, ctx) < 0) {
        flux_log (ctx->h, LOG_ERR, "kvs_watch_int: %s", strerror (errno));
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

MOD_NAME ("modctl");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
