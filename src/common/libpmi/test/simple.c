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

static int s_kvs_put (void *arg,
                      const char *kvsname,
                      const char *key,
                      const char *val)
{
    diag ("%s: %s::%s", __FUNCTION__, kvsname, key);
    struct context *ctx = arg;
    int rc = 0;
    zhash_update (ctx->kvs, key, xstrdup (val));
    zhash_freefn (ctx->kvs, key, (zhash_free_fn *)free);

    return rc;
}

static int s_kvs_get (void *arg,
                      void *client,
                      const char *kvsname,
                      const char *key)
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

static void s_io_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
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
    if (!(w = flux_fd_watcher_create (reactor,
                                      ctx->fds[1],
                                      FLUX_POLLIN,
                                      s_io_cb,
                                      ctx)))
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
    void *cli;
    struct pmi_operations *ops;
    int spawned = -1, initialized = -1;
    int rank = -1, size = -1;
    int universe_size = -1;
    int name_len = -1, key_len = -1, val_len = -1;
    char *name = NULL, *val = NULL, *val2 = NULL, *val3 = NULL;
    char *key = NULL;
    int rc;
    char port[1024];

    plan (NO_PLAN);

    if (!(ctx.kvs = zhash_new ()))
        oom ();
    ctx.size = 1;
    ok (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ctx.fds) == 0,
        "socketpair returned client,server file descriptors");
    ctx.pmi = pmi_simple_server_create (server_ops,
                                        42,
                                        ctx.size,
                                        ctx.size,
                                        "bleepgorp",
                                        0,
                                        &ctx);
    ok (ctx.pmi != NULL, "created simple pmi server context");
    ok (pthread_create (&ctx.t, NULL, server_thread, &ctx) == 0,
        "pthread_create successfully started server");

    setenvf ("PMI_FD", 1, "%d", ctx.fds[0]);
    setenvf ("PMI_RANK", 1, "%d", 0);
    setenvf ("PMI_SIZE", 1, "%d", ctx.size);

    ok ((cli = pmi_simple_client_create (&ops)) != NULL,
        "pmi_simple_client_create OK");
    ok (ops->initialized (cli, &initialized) == PMI_SUCCESS && initialized == 0,
        "pmi_simple_client_initialized OK, initialized=0");
    ok (ops->init (cli, &spawned) == PMI_SUCCESS && spawned == 0,
        "pmi_simple_client_init OK, spawned=0");
    ok (ops->initialized (cli, &initialized) == PMI_SUCCESS && initialized == 1,
        "pmi_simple_client_initialized OK, initialized=1");

    /* retrieve basic params
     */
    ok (ops->get_size (cli, &size) == PMI_SUCCESS && size == 1,
        "pmi_simple_client_get_size OK, size=%d",
        size);
    ok (ops->get_rank (cli, &rank) == PMI_SUCCESS && rank == 0,
        "pmi_simple_client_get_rank OK, rank=%d",
        rank);
    ok (ops->get_universe_size (cli, &universe_size) == PMI_SUCCESS
            && universe_size == size,
        "pmi_simple_client_get_universe_size OK, universe_size=%d",
        universe_size);
    ok (ops->kvs_get_name_length_max (cli, &name_len) == PMI_SUCCESS
            && name_len > 0,
        "pmi_simple_client_kvs_get_name_length_max OK, name_len=%d",
        name_len);
    ok (ops->kvs_get_key_length_max (cli, &key_len) == PMI_SUCCESS
            && key_len > 0,
        "pmi_simple_client_kvs_get_key_length_max OK, key_len=%d",
        key_len);
    ok (ops->kvs_get_value_length_max (cli, &val_len) == PMI_SUCCESS
            && val_len > 0,
        "pmi_simple_client_kvs_get_value_length_max OK, val_len=%d",
        val_len);
    name = xzmalloc (name_len);
    ok (ops->kvs_get_my_name (cli, name, name_len) == PMI_SUCCESS
            && strlen (name) > 0,
        "pmi_simple_client_kvs_get_my_name OK, name=%s",
        name);

    /* put foo=bar / commit / barier / get foo
     */
    ok (ops->kvs_put (cli, name, "foo", "bar") == PMI_SUCCESS,
        "pmi_simple_client_kvs_put foo=bar OK");
    ok (ops->kvs_commit (cli, name) == PMI_SUCCESS,
        "pmi_simple_client_kvs_commit OK");
    ok (ops->barrier (cli) == PMI_SUCCESS, "pmi_simple_client_barrier OK");
    val = xzmalloc (val_len);
    ok (ops->kvs_get (cli, name, "foo", val, val_len) == PMI_SUCCESS
            && !strcmp (val, "bar"),
        "pmi_simple_client_kvs_get foo OK, val=%s",
        val);

    /* put long=... / get long
     */
    val2 = xzmalloc (val_len);
    memset (val2, 'x', val_len - 1);
    ok (ops->kvs_put (cli, name, "long", val2) == PMI_SUCCESS,
        "pmi_simple_client_kvs_put long=xxx... OK");
    memset (val, 'y', val_len); /* not null terminated */
    ok (ops->kvs_get (cli, name, "long", val, val_len) == PMI_SUCCESS
            && strnlen (val2, val_len) < val_len && strcmp (val, val2) == 0,
        "pmi_simple_client_kvs_get long OK, val=xxx...");

    /* put: value too long
     */
    val3 = xzmalloc (val_len + 1);
    memset (val3, 'y', val_len);
    rc = ops->kvs_put (cli, name, "toolong", val3);
    ok (rc == PMI_ERR_INVALID_VAL_LENGTH,
        "pmi_simple_client_kvs_put val too long fails");

    /* put: key too long
     */
    key = xzmalloc (key_len + 1);
    memset (key, 'z', key_len);
    rc = ops->kvs_put (cli, name, key, "abc");
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_put key too long fails");

    /* get: key too long
     */
    rc = ops->kvs_get (cli, name, key, val, val_len);
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_get key too long fails");

    /* get: no exist
     */
    rc = ops->kvs_get (cli, name, "noexist", val, val_len);
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_simple_client_kvs_get unknown key fails");

    /* barrier: entry failure
     */
    rig_barrier_entry_failure = 1;
    ok (ops->barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with entry function failure fails");
    rig_barrier_entry_failure = 0;
    rig_barrier_exit_failure = 1;
    ok (ops->barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with exit function failure fails");
    rig_barrier_exit_failure = 0;
    ok (ops->barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK (rigged errors cleared)");

    /* unimplemented stuff
     */
    rc = ops->publish_name (cli, "foo", "42");
    ok (rc == PMI_FAIL, "pmi_simple_publish_name fails with PMI_FAIL");
    rc = ops->unpublish_name (cli, "foo");
    ok (rc == PMI_FAIL, "pmi_simple_unpublish_name fails with PMI_FAIL");
    rc = ops->lookup_name (cli, "foo", port);
    ok (rc == PMI_FAIL, "pmi_simple_lookup_name fails with PMI_FAIL");

    rc = ops->spawn_multiple (cli,
                              0,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              0,
                              NULL,
                              NULL);
    ok (rc == PMI_FAIL, "pmi_simple_spawn_multiple fails with PMI_FAIL");

    dies_ok ({ ops->abort (cli, 0, "a test message"); },
             "pmi_simple_abort exits program");

    /* finalize
     */

    ok (ops->finalize (cli) == PMI_SUCCESS, "pmi_simple_client_finalize OK");

    ok (pthread_join (ctx.t, NULL) == 0,
        "pthread join successfully reaped server");

    free (name);
    free (val);
    free (val2);
    free (val3);
    free (key);
    ops->destroy (cli);
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
