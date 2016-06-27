#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <czmq.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi-client/pmi-client.h"
#include "src/common/libpmi-server/simple.h"
#include "src/common/libflux/reactor.h"

#include "src/common/libtap/tap.h"

struct context {
    pthread_t t;
    int fds[2];
    int exit_rc;
    zhash_t *kvs;
    struct pmi_simple_server *pmi;
    int size;
    char *buf;
    int buflen;
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

static int s_kvs_get (void *arg, const char *kvsname, const char *key,
                 char *val, int len)
{
    diag ("%s: %s::%s", __FUNCTION__, kvsname, key);
    struct context *ctx = arg;
    char *v = zhash_lookup (ctx->kvs, key);
    int rc = -1;

    if (v && strlen (v) < len) {
        strcpy (val, v);
        rc = 0;
    }
    return rc;
}

static int dgetline (int fd, char *buf, int len)
{
    int i = 0;
    while (i < len - 1) {
        if (read (fd, &buf[i], 1) <= 0)
            return -1;
        if (buf[i++] == '\n')
            break;
    }
    if (buf[i - 1] != '\n') {
        errno = EPROTO;
        return -1;
    }
    buf[i] = '\0';
    return 0;
}

static int dputline (int fd, const char *buf)
{
    int len = strlen (buf);
    int n, count = 0;
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
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

    if (dgetline (fd, ctx->buf, ctx->buflen) < 0) {
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
    struct pmi_simple_ops ops = {
        .kvs_put = s_kvs_put,
        .kvs_get = s_kvs_get,
        .barrier_enter = NULL,
        .response_send = s_send_response,
    };
    pmi_t *cli;
    int spawned = -1, initialized = -1;
    int rank = -1, size = -1;
    int universe_size = -1;
    int name_len = -1, key_len = -1, val_len = -1;
    char *name = NULL, *val = NULL, *val2 = NULL;

    plan (NO_PLAN);

    if (!(ctx.kvs = zhash_new ()))
        oom ();
    ctx.size = 1;
    ok (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ctx.fds) == 0,
        "socketpair returned client,server file descriptors");
    ctx.pmi = pmi_simple_server_create (&ops, 42, ctx.size, ctx.size,
                                        "bleepgorp", &ctx);
    ok (ctx.pmi != NULL,
        "created simple pmi server context");
    ctx.buflen = pmi_simple_server_get_maxrequest (ctx.pmi);
    ctx.buf = xzmalloc (ctx.buflen);
    ok (pthread_create (&ctx.t, NULL, server_thread, &ctx) == 0,
        "pthread_create successfully started server");

    ok ((cli = pmi_create_simple (ctx.fds[0], 0, ctx.size)) != NULL,
        "pmi_create_simple OK");
    ok (pmi_initialized (cli, &initialized) == PMI_SUCCESS && initialized == 0,
        "pmi_initialized OK, initialized=0");
    ok (pmi_init (cli, &spawned) == PMI_SUCCESS && spawned == 0,
        "pmi_init OK, spawned=0");
    ok (pmi_initialized (cli, &initialized) == PMI_SUCCESS && initialized == 1,
        "pmi_initialized OK, initialized=1");

    /* retrieve basic params
     */
    ok (pmi_get_size (cli, &size) == PMI_SUCCESS && size == 1,
        "pmi_get_size OK, size=%d", size);
    ok (pmi_get_rank (cli, &rank) == PMI_SUCCESS && rank == 0,
        "pmi_get_rank OK, rank=%d", rank);
    ok (pmi_get_universe_size (cli, &universe_size) == PMI_SUCCESS
        && universe_size == size,
        "pmi_get_universe_size OK, universe_size=%d", universe_size);
    ok (pmi_kvs_get_name_length_max (cli, &name_len) == PMI_SUCCESS
        && name_len > 0,
        "pmi_kvs_get_name_length_max OK, name_len=%d", name_len);
    ok (pmi_kvs_get_key_length_max (cli, &key_len) == PMI_SUCCESS
        && key_len > 0,
        "pmi_kvs_get_key_length_max OK, key_len=%d", key_len);
    ok (pmi_kvs_get_value_length_max (cli, &val_len) == PMI_SUCCESS
        && val_len > 0,
        "pmi_kvs_get_value_length_max OK, val_len=%d", val_len);
    name = xzmalloc (name_len);
    ok (pmi_kvs_get_my_name (cli, name, name_len) == PMI_SUCCESS
        && strlen (name) > 0,
        "pmi_kvs_get_my_name OK, name=%s", name);

    /* put foo=bar / commit / barier / get foo
     */
    ok (pmi_kvs_put (cli, name, "foo", "bar") == PMI_SUCCESS,
        "pmi_kvs_put foo=bar OK");
    ok (pmi_kvs_commit (cli, name) == PMI_SUCCESS,
        "pmi_kvs_commit OK");
    ok (pmi_barrier (cli) == PMI_SUCCESS,
        "pmi_barrier OK");
    val = xzmalloc (val_len);
    ok (pmi_kvs_get (cli, name, "foo", val, val_len) == PMI_SUCCESS
        && !strcmp (val, "bar"),
        "pmi_kvs_get foo OK, val=%s", val);

    /* put long=... / get long
     */
    val2 = xzmalloc (val_len);
    memset (val2, 'x', val_len - 1);
    ok (pmi_kvs_put (cli, name, "long", val2) == PMI_SUCCESS,
        "pmi_kvs_put long=xxx... OK");
    memset (val, 'y', val_len); /* not null terminated */
    ok (pmi_kvs_get (cli, name, "long", val, val_len) == PMI_SUCCESS
        && strnlen (val2, val_len) < val_len
        && strcmp (val, val2) == 0,
        "pmi_kvs_get long OK, val=xxx...");

    ok (pmi_finalize (cli) == PMI_SUCCESS,
        "pmi_finalize OK");

    ok (pthread_join (ctx.t, NULL) == 0,
        "pthread join successfully reaped server");

    free (name);
    free (val);
    free (val2);
    pmi_destroy (cli);
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
