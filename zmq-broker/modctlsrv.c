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
#include "hostlist.h"

typedef struct {
    flux_t h;
    red_t r;
    bool master;
} ctx_t;

static const int red_timeout_msec_master = 20;
static const int red_timeout_msec_slave = 2;

static void modctl_reduce (flux_t h, zlist_t *items, int batchnum, void *arg);
static void modctl_sink (flux_t h, void *item, int batchnum, void *arg);

static void freectx (ctx_t *ctx)
{
    flux_red_destroy (ctx->r);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "modctlsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->master = flux_treeroot (h);
        ctx->r = flux_red_create (h, modctl_sink, ctx);
        if (ctx->master)
            flux_red_set_timeout_msec (ctx->r, red_timeout_msec_master);
        else
            flux_red_set_timeout_msec (ctx->r, red_timeout_msec_slave);
        flux_red_set_reduce_fn (ctx->r, modctl_reduce);
        flux_red_set_flags (ctx->r, FLUX_RED_TIMEDFLUSH);
        flux_aux_set (h, "modctlsrv", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

char *hl_string (hostlist_t hl)
{
    int len = 64;
    char *s = xzmalloc (len);

    hostlist_sort (hl);
    while (hostlist_ranged_string (hl, len, s) < 0)
        if (!(s = realloc (s, len *= 2)))
            oom ();
    len = strlen (s);
    return s;
}

/* Combine a and b into a new string (returned, caller must free).
 */
char *merge_nodelist (const char *a, const char *b)
{
    char *s = NULL;
    hostlist_t hl;

    if (!(hl = hostlist_create (a)) || !hostlist_push (hl, b))
        goto done;
    s = hl_string (hl);
done:
    if (hl)
        hostlist_destroy (hl);
    return s;
}

int merge_idle (int a, int b)
{
    return (a < b ? a : b);
}

/* Merge b into a.
 */
static void merge_mods (JSON a, JSON b)
{
    json_object_iter iter;
    JSON am;
    char *s;
    const char *anl, *bnl;
    int ai, bi, i;

    json_object_object_foreachC (b, iter) {

        /* module in b is new to a */
        if (!Jget_obj (a, iter.key, &am)) {
            Jadd_obj (a, iter.key, iter.val);
            continue;
        }

        /* merge nodelists */
        if (!Jget_str (iter.val, "nodelist", &bnl)
                                        || !Jget_str (am, "nodelist", &anl))
            continue;
        s = merge_nodelist (anl, bnl);
        json_object_object_del (am, "nodelist");
        Jadd_str (am, "nodelist", s);
        free (s);

        /* merge idle time */
        if (!Jget_int (iter.val, "idle", &bi) || !Jget_int (am, "idle", &ai))
            continue;
        i = merge_idle (ai, bi);
        json_object_object_del (am, "idle");
        Jadd_int (am, "idle", i);
    }
}

static void modctl_reduce (flux_t h, zlist_t *items, int batchnum, void *arg)
{
    JSON a, b, amods, bmods;

    if ((a = zlist_pop (items))) {
        while ((b = zlist_pop (items))) {
            if (Jget_obj (a, "mods", &amods) && Jget_obj (b, "mods", &bmods))
                merge_mods (amods, bmods);
            Jput (b);
        }
        zlist_append (items, a);
    }
}

static void modctl_sink (flux_t h, void *item, int batchnum, void *arg)
{
    ctx_t *ctx = arg;
    JSON a = item;
    JSON b = NULL, amods, bmods;
    int seq;

    if (ctx->master) {  /* sink to KVS */
        if (kvs_get (h, "conf.modctl.lsmod", &b) == 0) {
            if (Jget_int (b, "seq", &seq) && seq == batchnum
                                          && Jget_obj (a, "mods", &amods)
                                          && Jget_obj (b, "mods", &bmods))
                merge_mods (amods, bmods);
            Jput (b);
        }
        if (kvs_put (h, "conf.modctl.lsmod", a) < 0 || kvs_commit (h) < 0) {
            flux_log (ctx->h, LOG_ERR, "%s: %s", __FUNCTION__,strerror (errno));
        }
    } else {            /* push pustream */
        flux_request_send (h, a, "modctl.push");
    }
    Jput (a);
}

static int push_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
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
    zmsg_destroy (zmsg);
    return 0;
}

static int lsmod_reduce (ctx_t *ctx, int seq)
{
    JSON o = NULL;
    JSON lsmod = NULL;
    int rc = -1;

    if (!(lsmod = flux_lsmod (ctx->h, -1)))
        goto done;
    o = Jnew ();
    Jadd_int (o, "seq", seq);
    Jadd_obj (o, "mods", lsmod);
    flux_red_append (ctx->r, o, seq); /* takes ownership of 'o' */
done:
    Jput (lsmod);
    return rc;
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

/* Install module out of KVS.
 * KVS content is copied to a tmp file, dlopened, and immediately unlinked.
 */
static int installmod (ctx_t *ctx, const char *name)
{
    char *key = NULL;
    JSON mod = NULL, args;
    uint8_t *buf = NULL;
    int fd = -1, len;
    char tmpfile[] = "/tmp/flux-modctl-XXXXXX"; /* FIXME: consider TMPDIR */
    int n, rc = -1;
    int errnum = 0;

    if (asprintf (&key, "conf.modctl.modules.%s", name) < 0)
        oom ();
    if (kvs_get (ctx->h, key, &mod) < 0 || !Jget_obj (mod, "args", &args)
            || util_json_object_get_data (mod, "data", &buf, &len) < 0) {
        errnum = EPROTO;
        goto done; /* kvs/parse error */
    }
    if ((fd = mkstemp (tmpfile)) < 0) {
        errnum = errno;
        goto done;
    }
    if (write_all (fd, buf, len) < 0) {
        errnum = errno;
        goto done;
    }
    n = close (fd);
    fd = -1;
    if (n < 0) {
        errnum = errno;
        goto done;
    }
    if (flux_insmod (ctx->h, -1, tmpfile, FLUX_MOD_FLAGS_MANAGED, args) < 0) {
        errnum = errno;
        goto done_unlink;
    }
    rc = 0;
done_unlink:
    (void)unlink (tmpfile);
done:
    if (fd != -1)
        (void)close (fd);
    if (key)
        free (key);
    if (buf)
        free (buf);
    Jput (mod);
    if (errnum != 0)
        errno = errnum;
    return rc;
}

/* This function is called whenver conf.modctl.seq is updated by master.
 * It syncs the set of loaded modules with the KVS.
 */
static void conf_cb (const char *path, int seq, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    kvsitr_t itr;
    JSON mod, lsmod = NULL;
    json_object_iter iter;
    const char *name;
    kvsdir_t dir;

    if (errnum == ENOENT) {
        seq = 0;
        goto done_lsmod; /* not initialized */
    }
    if (errnum != 0) {
        flux_log (ctx->h, LOG_ERR, "%s", "conf.modctl.seq");
        goto done;
    }
    if (ctx->master) { /* master already loaded/unloaded module */
        goto done_lsmod;
    }
    if (!(lsmod = flux_lsmod (ctx->h, -1))) {
        flux_log (ctx->h, LOG_ERR, "flux_lsmod: %s", strerror (errno));
        goto done;
    }
    /* Walk through list of modules that should be installed (from kvs),
     * insmod-ing any that are not not.
     */
    if (kvs_get_dir (ctx->h, &dir, "conf.modctl.modules") == 0) {
        if (!(itr = kvsitr_create (dir)))
            oom ();
        while ((name = kvsitr_next (itr))) {
            if (!Jget_obj (lsmod, name, &mod))
                if (installmod (ctx, name) < 0)
                    flux_log (ctx->h, LOG_ERR, "installmod %s: %s", name,
                              strerror (errno));
        }
        kvsitr_destroy (itr);
    }
    /* Walk through the list of modules that are installed (from lsmod),
     * rmmod-ing any that should not be.
     */
    json_object_object_foreachC (lsmod, iter) {
        int fl;
        char *key;
        if (!Jget_int (iter.val, "flags",&fl) || !(fl & FLUX_MOD_FLAGS_MANAGED))
            continue;
        if (asprintf (&key, "conf.modctl.modules.%s", iter.key) < 0)
            oom ();
        if (kvs_get (ctx->h, key, &mod) < 0) {
            if (flux_rmmod (ctx->h, -1, iter.key, FLUX_MOD_FLAGS_MANAGED) < 0)
                flux_log (ctx->h, LOG_ERR, "flux_rmmod %s: %s", iter.key,
                          strerror (errno));
        } else
            Jput (mod);
        free (key);
    }
done_lsmod:
    /* Fetch (now modified) list of installed modules.
     * Push it through the reduction network (ultimately to the KVS on master).
     */
    lsmod_reduce (ctx, seq);
done:
    Jput (lsmod);
}

static int seq_incr (flux_t h)
{
    int seq = 0;
    const char *key = "conf.modctl.seq";

    (void)kvs_get_int (h, key, &seq);
    if (kvs_put_int (h, key, ++seq) < 0 || kvs_commit (h) < 0)
        return -1;
    return 0;
}

static int ins_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    const char *name;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
            || !Jget_str (request, "name", &name)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (ctx->master) {
        if (installmod (ctx, name) < 0 || seq_incr (h) < 0) {
            flux_respond_errnum (h, zmsg, errno);
            goto done;
        }
        flux_respond_errnum (h, zmsg, 0);
    } else {
        flux_request_sendmsg (h, zmsg);
    }
done:
    Jput (request);
    return rc;
}

static int rm_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    const char *name;
    int fl = FLUX_MOD_FLAGS_MANAGED;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
            || !Jget_str (request, "name", &name)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (ctx->master) {
        if (flux_rmmod (ctx->h, -1, name, fl) < 0 || seq_incr (h) < 0) {
            flux_respond_errnum (h, zmsg, errno);
            goto done;
        }
        flux_respond_errnum (h, zmsg, 0);
    } else {
        flux_request_sendmsg (h, zmsg);
    }
done:
    Jput (request);
    return rc;
}

static int update_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (ctx->master) {
        if (seq_incr (h) < 0) {
            flux_respond_errnum (h, zmsg, errno);
            goto done;
        }
        flux_respond_errnum (h, zmsg, 0);
    } else {
        flux_request_sendmsg (h, zmsg);
    }
done:
    Jput (request);
    return rc;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST, "modctl.push",              push_request_cb },
    { FLUX_MSGTYPE_REQUEST, "modctl.ins",               ins_request_cb },
    { FLUX_MSGTYPE_REQUEST, "modctl.rm",                rm_request_cb },
    { FLUX_MSGTYPE_REQUEST, "modctl.update",            update_request_cb },
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
