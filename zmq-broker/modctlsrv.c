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

typedef struct {
    flux_t h;
    zhash_t *modules;
    char *tmpdir;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->modules);
    (void)rmdir (ctx->tmpdir);
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
        if (asprintf (&ctx->tmpdir, "/tmp/flux-modctl.XXXXXX") < 0)
            oom ();
        if (!mkdtemp (ctx->tmpdir))
            err_exit ("mkdtemp");
        flux_aux_set (h, "modctlsrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
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

static void conf_cb (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    kvsitr_t itr;
    JSON mod;
    const char *name;
    zlist_t *keys;
    char *key;

    /* Install managed modules listed in kvs.
     */
    if (errnum == 0) {
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
}

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_dir (h, conf_cb, ctx, "conf.modctl.modules") < 0) {
        flux_log (ctx->h, LOG_ERR, "kvs_watch_dir: %s", strerror (errno));
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
