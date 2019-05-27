/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <czmq.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libpmi/simple_client.h"
#include "src/common/libpmi/dgetline.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libflux/reactor.h"

#include "src/common/libtap/tap.h"

struct context {
    pthread_t t;
    int fds[2];
    int exit_rc;
    zhash_t *kvs;
    struct pmi_simple_server *pmi;
    int size;
    char buf[SIMPLE_MAX_PROTO_LINE];
};

static int s_kvs_put (void *arg, const char *kvsname, const char *key,
                 const char *val)
{
    diag ("%s: %s::%s", __FUNCTION__, kvsname, key);
    struct context *ctx = arg;
    int rc = 0;
    zhash_update (ctx->kvs, key, xstrdup (val));
    zhash_freefn (ctx->kvs, key, (zhash_free_fn *)free);

    return rc;
}

static int s_kvs_get (void *arg, void *client,
                      const char *kvsname, const char *key)
{
    diag ("%s: %s::%s", __FUNCTION__, kvsname, key);
    struct context *ctx = arg;
    char *v = zhash_lookup (ctx->kvs, key);
    pmi_simple_server_kvs_get_complete (ctx->pmi, client, v);
    return 0;
}

static int s_send_response (void *client, const char *buf)
{
    int *rfd = client;
    return dputline (*rfd, buf);
}

static void s_io_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct context *ctx = arg;
    int fd = flux_fd_watcher_get_fd (w);
    int rc;

    if (dgetline (fd, ctx->buf, sizeof (ctx->buf)) < 0) {
        diag ("dgetline: %s", strerror (errno));
        flux_reactor_stop_error (r);
        return;
    }
    rc = pmi_simple_server_request (ctx->pmi, ctx->buf, &ctx->fds[1]);
    if (rc < 0) {
        diag ("pmi_simple_server_request: %s", strerror (errno));
        flux_reactor_stop_error (r);
        return;
    }
    if (rc == 1) {
        close (fd);
        flux_watcher_stop (w);
    }
}

static int rig_barrier_entry_failure = 0;
static int rig_barrier_exit_failure = 0;

static int s_barrier_enter (void *arg)
{
    struct context *ctx = arg;
    if (rig_barrier_entry_failure)
        return -1;
    if (rig_barrier_exit_failure)
        pmi_simple_server_barrier_complete (ctx->pmi, PMI_FAIL);
    else
        pmi_simple_server_barrier_complete (ctx->pmi, 0);
    return 0;
}

void *server_thread (void *arg)
{
    struct context *ctx = arg;
    flux_reactor_t *reactor = NULL;
    flux_watcher_t *w = NULL;

    if (!(reactor = flux_reactor_create (0)))
        goto done;
    if (!(w = flux_fd_watcher_create (reactor, ctx->fds[1],
                                      FLUX_POLLIN, s_io_cb, ctx)))
        goto done;
    flux_watcher_start (w);

    ctx->exit_rc = -1;
    if (flux_reactor_run (reactor, 0) < 0)
        goto done;

    ctx->exit_rc = 0;
done:
    if (w)
        flux_watcher_destroy (w);
    if (reactor)
        flux_reactor_destroy (reactor);
    return NULL;
}

int main (int argc, char *argv[])
{
    struct context ctx;
    struct pmi_simple_ops server_ops = {
        .kvs_put = s_kvs_put,
        .kvs_get = s_kvs_get,
        .barrier_enter = s_barrier_enter,
        .response_send = s_send_response,
    };
    struct pmi_simple_client *cli;
    int universe_size = -1;
    char *name = NULL, *val = NULL, *val2 = NULL, *val3 = NULL;
    char *key = NULL;
    int rc;
    char pmi_fd[16];
    char pmi_rank[16];
    char pmi_size[16];

    plan (NO_PLAN);

    if (!(ctx.kvs = zhash_new ()))
        oom ();
    ctx.size = 1;
    ok (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ctx.fds) == 0,
        "socketpair returned client,server file descriptors");
    ctx.pmi = pmi_simple_server_create (server_ops, 42, ctx.size, ctx.size,
                                        "bleepgorp", 0, &ctx);
    ok (ctx.pmi != NULL,
        "created simple pmi server context");
    ok (pthread_create (&ctx.t, NULL, server_thread, &ctx) == 0,
        "pthread_create successfully started server");

    /* create/init
     */
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", ctx.fds[0]);
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);

    ok ((cli = pmi_simple_client_create_fd (pmi_fd, pmi_rank, pmi_size,
                                            NULL, NULL)) != NULL,
        "pmi_simple_client_create OK");
    ok (cli->initialized == false,
        "cli->initialized == false");
    ok (pmi_simple_client_init (cli) == PMI_SUCCESS,
        "pmi_simple_client_init OK");
    ok (cli->spawned == false,
        "cli->spawned == failse");

    /* retrieve basic params
     */
    ok (cli->size == 1,
        "cli->size == 1");
    ok (cli->rank == 0,
        "cli->rank == 0");
    ok (pmi_simple_client_get_universe_size (cli, &universe_size) == PMI_SUCCESS
        && universe_size == cli->size,
        "pmi_simple_client_get_universe_size OK, universe_size=%d", universe_size);
    ok (cli->kvsname_max > 0,
        "cli->kvsname_max > 0");
    ok (cli->keylen_max > 0,
        "cli->keylen_max > 0");
    ok (cli->vallen_max > 0,
        "cli->vallen_max > 0");
    name = xzmalloc (cli->kvsname_max);
    ok (pmi_simple_client_kvs_get_my_name (cli, name,
                                           cli->kvsname_max) == PMI_SUCCESS
        && strlen (name) > 0,
        "pmi_simple_client_kvs_get_my_name OK");
    diag ("kvsname=%s", name);

    /* put foo=bar / commit / barier / get foo
     */
    ok (pmi_simple_client_kvs_put (cli, name, "foo", "bar") == PMI_SUCCESS,
        "pmi_simple_client_kvs_put foo=bar OK");
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK");
    val = xzmalloc (cli->vallen_max);
    ok (pmi_simple_client_kvs_get (cli, name, "foo",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && !strcmp (val, "bar"),
        "pmi_simple_client_kvs_get foo OK, val=%s", val);

    /* put long=... / get long
     */
    val2 = xzmalloc (cli->vallen_max);
    memset (val2, 'x', cli->vallen_max - 1);
    ok (pmi_simple_client_kvs_put (cli, name, "long", val2) == PMI_SUCCESS,
        "pmi_simple_client_kvs_put long=xxx... OK");
    memset (val, 'y', cli->vallen_max); /* not null terminated */
    ok (pmi_simple_client_kvs_get (cli, name, "long",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && strnlen (val2, cli->vallen_max) < cli->vallen_max
        && strcmp (val, val2) == 0,
        "pmi_simple_client_kvs_get long OK, val=xxx...");

    /* put: value too long
     */
    val3 = xzmalloc (cli->vallen_max + 1);
    memset (val3, 'y', cli->vallen_max);
    rc = pmi_simple_client_kvs_put (cli, name, "toolong", val3);
    ok (rc == PMI_ERR_INVALID_VAL_LENGTH,
        "pmi_simple_client_kvs_put val too long fails");

    /* put: key too long
     */
    key = xzmalloc (cli->keylen_max + 1);
    memset (key, 'z', cli->keylen_max);
    rc = pmi_simple_client_kvs_put (cli, name, key, "abc");
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_put key too long fails");

    /* get: key too long
     */
    rc = pmi_simple_client_kvs_get (cli, name, key, val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_get key too long fails");

    /* get: no exist
     */
    rc = pmi_simple_client_kvs_get (cli, name, "noexist", val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_simple_client_kvs_get unknown key fails");

    /* barrier: entry failure
     */
    rig_barrier_entry_failure = 1;
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with entry function failure fails");
    rig_barrier_entry_failure = 0;
    rig_barrier_exit_failure = 1;
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with exit function failure fails");
    rig_barrier_exit_failure = 0;
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK (rigged errors cleared)");

    /* finalize
     */

    ok (pmi_simple_client_finalize (cli) == PMI_SUCCESS,
        "pmi_simple_client_finalize OK");

    ok (pthread_join (ctx.t, NULL) == 0,
        "pthread join successfully reaped server");

    free (name);
    free (val);
    free (val2);
    free (val3);
    free (key);
    pmi_simple_client_destroy (cli);
    if (ctx.pmi)
        pmi_simple_server_destroy (ctx.pmi);
    close (ctx.fds[0]);
    close (ctx.fds[1]);
    zhash_destroy (&ctx.kvs);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
