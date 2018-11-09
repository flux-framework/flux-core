#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>
#include <pthread.h>

#include "util.h"

#include "src/common/libtap/tap.h"
#include "src/common/libutil/setenvf.h"

struct test_server {
    flux_t *c;
    flux_t *s;
    flux_msg_handler_t *mh;
    test_server_f cb;
    void *arg;
    pthread_t thread;
    int rc;
    zuuid_t *uuid;
};

static flux_t *test_connector_create (const char *shmem_name,
                                      bool server, int flags);

void shutdown_cb (flux_t *h, flux_msg_handler_t *mh,
                  const flux_msg_t *msg, void *arg)
{
    flux_reactor_stop (flux_get_reactor (h));
}

static void *thread_wrapper (void *arg)
{
    struct test_server *a = arg;

    if (a->cb (a->s, a->arg) < 0)
        a->rc = -1;
    else
        a->rc = 0;

    return NULL;
}

int test_server_stop (flux_t *c)
{
    int e;
    flux_msg_t *msg = NULL;
    int rc = -1;
    struct test_server *a = flux_aux_get (c, "test_server");

    if (!a) {
        diag ("%s: flux_aux_get failed\n", __FUNCTION__);
        errno = EINVAL;
        goto done;
    }
    if (!(msg = flux_request_encode ("shutdown", NULL)))
        goto done;
    if (flux_send (a->c, msg, 0) < 0) {
        diag ("%s: flux_send: %s\n", __FUNCTION__, flux_strerror (errno));
        goto done;
    }
    if ((e = pthread_join (a->thread, NULL)) != 0) {
        errno = e;
        diag ("%s: pthread_join: %s\n", __FUNCTION__, strerror (errno));
        goto done;
    }
    rc = a->rc;
done:
    flux_msg_destroy (msg);
    return rc;
}

static void test_server_destroy (struct test_server *a)
{
    if (a) {
        flux_msg_handler_destroy (a->mh);
        flux_close (a->s);
        zuuid_destroy (&a->uuid);
        free (a);
    }
}

flux_t *test_server_create (test_server_f cb, void *arg)
{
    int e;
    struct test_server *a;

    if (!(a = calloc (1, sizeof (*a))))
        BAIL_OUT ("calloc");
    if (!cb)
        BAIL_OUT ("test_server created called without server callback");
    a->cb = cb;
    a->arg = arg;

    if (!(a->uuid = zuuid_new ()))
        BAIL_OUT ("zuuid_new failed");

    /* Create back-to-back wired flux_t handles
     */
    if (!(a->s = test_connector_create (zuuid_str (a->uuid), true, 0)))
        BAIL_OUT ("test_connector_create server");
    if (!(a->c = test_connector_create (zuuid_str (a->uuid), false, 0)))
        BAIL_OUT ("test_connector_create client");

    /* Register watcher for "shutdown" request on server side
     */
    struct flux_match match = FLUX_MATCH_REQUEST;
    match.topic_glob = "shutdown";
    if (!(a->mh = flux_msg_handler_create (a->s, match, shutdown_cb, a))) {
        diag ("%s: flux_msg_handler_create: %s\n",
             __FUNCTION__, flux_strerror (errno));
        goto error;
    }
    flux_msg_handler_start (a->mh);

    /* Start server thread
     */
    if ((e = pthread_create (&a->thread, NULL, thread_wrapper, a)) != 0) {
        errno = e;
        diag ("%s: pthread_create: %s\n",
              __FUNCTION__, strerror (errno));
        goto error;
    }
    flux_aux_set (a->c, "test_server", a,
                  (flux_free_f)test_server_destroy);
    return a->c;
error:
    test_server_destroy (a);
    return NULL;
}

void test_server_environment_init (const char *test_name)
{
    zsys_init ();
    zsys_set_logstream (stderr);
    zsys_set_logident (test_name);
    zsys_handler_set (NULL);
    zsys_set_linger (5); // msec
}

/* Test connector implementation
 */

struct test_connector {
    zsock_t *sock;
    flux_t *h;
    char *uuid;
    uint32_t userid;
    uint32_t rolemask;
};

static int test_connector_pollevents (void *impl)
{
    struct test_connector *tcon = impl;
    int e = zsock_events (tcon->sock);
    int revents = 0;

    if (e & ZMQ_POLLIN)
        revents |= FLUX_POLLIN;
    if (e & ZMQ_POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & ZMQ_POLLERR)
        revents |= FLUX_POLLERR;

    return revents;
}

static int test_connector_pollfd (void *impl)
{
    struct test_connector *tcon = impl;

    return zsock_fd (tcon->sock);
}

static int test_connector_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct test_connector *tcon = impl;
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_set_userid (cpy, tcon->userid) < 0)
        goto error;
    if (flux_msg_set_rolemask (cpy, tcon->rolemask) < 0)
        goto error;
    if (flux_msg_sendzsock (tcon->sock, cpy) < 0)
        goto error;
    flux_msg_destroy (cpy);
    return 0;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static flux_msg_t *test_connector_recv (void *impl, int flags)
{
    struct test_connector *tcon = impl;

    if ((flags & FLUX_O_NONBLOCK)) {
        zmq_pollitem_t zp = {
            .events = ZMQ_POLLIN,
            .socket = zsock_resolve (tcon->sock),
            .revents = 0,
            .fd = -1,
        };
        int n;
        if ((n = zmq_poll (&zp, 1, 0L)) <= 0) {
            if (n == 0)
                errno = EWOULDBLOCK;
            return NULL;
        }
    }
    return flux_msg_recvzsock (tcon->sock);
}

static void test_connector_fini (void *impl)
{
    struct test_connector *tcon = impl;

    zsock_destroy (&tcon->sock);
    free (tcon);
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = test_connector_pollfd,
    .pollevents = test_connector_pollevents,
    .send = test_connector_send,
    .recv = test_connector_recv,
    .getopt = NULL,
    .setopt = NULL,
    .event_subscribe = NULL,
    .event_unsubscribe = NULL,
    .impl_destroy = test_connector_fini,
};

static flux_t *test_connector_create (const char *shmem_name,
                                      bool server, int flags)
{
    struct test_connector *tcon;

    if (!(tcon = calloc (1, sizeof (*tcon))))
        BAIL_OUT ("calloc");
    tcon->userid = geteuid ();
    tcon->rolemask = FLUX_ROLE_OWNER;
    if (!(tcon->sock = zsock_new_pair (NULL)))
        BAIL_OUT ("zsock_new_pair");
    zsock_set_unbounded (tcon->sock);
    if (server) {
        if (zsock_bind (tcon->sock, "inproc://%s", shmem_name) < 0)
            BAIL_OUT ("zsock_bind %s", shmem_name);
    }
    else {
        if (zsock_connect (tcon->sock, "inproc://%s", shmem_name) < 0)
            BAIL_OUT ("zsock_connect %s", shmem_name);
    }
    if (!(tcon->h = flux_handle_create (tcon, &handle_ops, flags)))
        BAIL_OUT ("flux_handle_create");
    return tcon->h;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
