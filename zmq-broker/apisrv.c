/* apisrv.c - bridge unix domain API socket and zmq message bus */

/* FIXME: consider adding SO_PEERCRED info for connected clients? */

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
#include <zmq.h>
#include <uuid/uuid.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "apisrv.h"

#define LISTEN_BACKLOG      5

typedef struct _client_struct {
    int fd;
    struct _client_struct *next;
    struct _client_struct *prev;
    char *subscription;
    char uuid[64];
} client_t;

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    char sockname[MAXPATHLEN];
    void *zs_in;
    void *zs_out;
    pthread_t t;
    int listen_fd;
    client_t *clients;
    char buf[CMB_API_BUFSIZE];
};

static ctx_t ctx = NULL;

static void _oom (void)
{
    fprintf (stderr, "out of memory\n");
    exit (1);
}

static void *_zmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        _oom ();
    memset (new, 0, size);
    return new;
}

static client_t *_client_create (int fd)
{
    client_t *c;

    c = _zmalloc (sizeof (client_t));
    c->fd = fd;
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
    return c;
}

static void _client_destroy (client_t *c)
{
    close (c->fd);
    if (c->prev)
        c->prev->next = c->next;
    else
        ctx->clients = c->next;
    if (c->next)
        c->next->prev = c->prev;
    if (strlen (c->uuid) > 0) {
        zmq_2part_t msg;
        _zmq_2part_init_empty (&msg, "event.%s.disconnect", c->uuid);
        _zmq_2part_send (ctx->zs_out, &msg, 0);
    }
    free (c);
}

static int _client_count (void)
{
    client_t *c;
    int count = 0;

    for (c = ctx->clients; c != NULL; c = c->next)
        count++;
    return count;
}

static void _accept ()
{
    client_t *c;
    int fd;

    fd = accept (ctx->listen_fd, NULL, NULL); 
    if (fd < 0) {
        fprintf (stderr, "apisrv: accept: %s\n", strerror (errno));
        exit (1);
    }
    c = _client_create (fd);
}

/* route API socket to broker (in: tag\0body, out: zmq) */
static int _client_read (client_t *c)
{
    int n, i;
    zmq_2part_t msg;
    int bodylen;
    char *tag, *body;

again:
    n = recv (c->fd, ctx->buf, sizeof (ctx->buf), MSG_DONTWAIT);
    if (n < 0 && errno == EWOULDBLOCK)
        return 0;
    if (n < 0) {
        if (errno != ECONNRESET)
            fprintf (stderr, "apisrv: API read: %s\n", strerror (errno));
        return -1;
    }
    if (n == 0) /* EOF */
        return -1;
    for (i = 0; i < n; i++) {
        if (ctx->buf[i] == '\0')
            break;
    }
    bodylen = n - i - 1;
    if (bodylen < 0) {
        fprintf (stderr, "apisrv: API read: malformed message\n");
        return -1;
    }
    body = &ctx->buf[i + 1];
    tag = &ctx->buf[0];

    if (!strcmp (tag, "subscribe")) { /* bodylen == 0 subscribes to "" (all) */
        if (c->subscription)
            free (c->subscription);
        c->subscription = _zmalloc (bodylen + 1);
        memcpy (c->subscription, body, bodylen);
    } else if (!strcmp (tag, "unsubscribe")) {
        if (c->subscription) {
            free (c->subscription);
            c->subscription = NULL;
        }
    } else if (!strcmp (tag, "setuuid")) {
        if (sizeof (c->uuid) >= bodylen + 1) {
            memcpy (c->uuid, body, bodylen); 
            _zmq_2part_init_empty (&msg, "event.%s.connect", c->uuid);
            _zmq_2part_send (ctx->zs_out, &msg, 0);
        } else
            return -1;
    } else {
        _zmq_2part_init_buf (&msg, body, bodylen, "%s", tag);
        _zmq_2part_send (ctx->zs_out, &msg, 0);
    }
    goto again;
    return 0;
}

/* route broker message to API socket (in: zmq, out: tag\0body) */
static void _readmsg (bool *shutdownp)
{
    zmq_2part_t msg;
    client_t *c;
    int len, n;

    _zmq_2part_init (&msg);
    _zmq_2part_recv (ctx->zs_in, &msg, 0);

    if (_zmq_2part_match (&msg, "event.cmb.shutdown")) {
        *shutdownp = true;
        goto done;
    }

    len = zmq_msg_size (&msg.tag) + zmq_msg_size (&msg.body) + 1;
    if (len > sizeof (ctx->buf)) {
        fprintf (stderr, "apisrv: dropping giant message\n");
        goto done;
    }

    memcpy (ctx->buf, zmq_msg_data (&msg.tag), zmq_msg_size (&msg.tag));
    ctx->buf[zmq_msg_size (&msg.tag)] = '\0';
    memcpy (ctx->buf + zmq_msg_size (&msg.tag) + 1, zmq_msg_data (&msg.body),
            len - zmq_msg_size (&msg.tag) - 1);

    /* send it to all API clients whose subscription matches */
    for (c = ctx->clients; c != NULL; c = c->next) {
        if (c->subscription && _zmq_2part_match (&msg, c->subscription)) {
            n = send (c->fd, ctx->buf, len, 0);
            if (n < len) {
                fprintf (stderr, "apisrv: API write: %s\n", strerror (errno));
                c = c->prev;
                _client_destroy (c);
            }
        }
    }
done:
    _zmq_2part_close (&msg);
}

static bool _poll (void)
{
    client_t *c, *deleteme;
    bool shutdown = false;
    int zpa_len = _client_count () + 2;
    zmq_pollitem_t *zpa = _zmalloc (sizeof (zmq_pollitem_t) * zpa_len);
    int i;

    zpa[0].socket = ctx->zs_in;
    zpa[0].events = ZMQ_POLLIN;
    zpa[0].fd = -1;
    zpa[1].events = ZMQ_POLLIN | ZMQ_POLLERR;
    zpa[1].fd = ctx->listen_fd;
 
    for (i = 2, c = ctx->clients; c != NULL; c = c->next, i++) {
        zpa[i].events = ZMQ_POLLIN | ZMQ_POLLERR;
        zpa[i].fd = c->fd;
    }
    assert (i == zpa_len);

    if (zmq_poll (zpa, zpa_len, -1) < 0) {
        fprintf (stderr, "apisrv: zmq_poll: %s\n", strerror (errno));
        exit (1);
    }

    for (i = 2, c = ctx->clients; c != NULL; i++) {
        assert (c->fd == zpa[i].fd);
        deleteme = NULL;
        if (zpa[i].revents & ZMQ_POLLIN) {
            if (_client_read (c) < 0)
                deleteme = c;
        }
        if (zpa[i].revents & ZMQ_POLLERR)
            deleteme = c;
        c = c->next;
        if (deleteme)
            _client_destroy (deleteme);
    }

    if (zpa[1].revents & ZMQ_POLLIN)
        _accept (ctx);
    if (zpa[1].revents & ZMQ_POLLERR) {
        fprintf (stderr, "apisrv: poll error on listen fd\n");
        exit (1);
    }
    if (zpa[0].revents & ZMQ_POLLIN)
        _readmsg (&shutdown);

    free (zpa);

    return ! shutdown;
}

static void _listener_init (void)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket (AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        fprintf (stderr, "socket: %s\n", strerror (errno));
        exit (1);
    }

    if (remove (ctx->sockname) < 0 && errno != ENOENT) {
        fprintf (stderr, "remove %s: %s\n", ctx->sockname, strerror (errno));
        exit (1);
    } 

    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, ctx->sockname, sizeof (addr.sun_path) - 1);

    if (bind (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un)) < 0) {
        fprintf (stderr, "bind: %s\n", strerror (errno));
        exit (1);
    }

    if (listen (fd, LISTEN_BACKLOG) < 0) {
        fprintf (stderr, "listen: %s\n", strerror (errno));
        exit (1);
    }

    ctx->listen_fd = fd;
}

static void _listener_fini (void)
{
    if (close (ctx->listen_fd) < 0) {
        fprintf (stderr, "listen: %s\n", strerror (errno));
        exit (1);
    }
}

static void *_thread (void *arg)
{
    _listener_init ();
    while (_poll ())
        ;
    _listener_fini ();
    return NULL;
}

void apisrv_init (conf_t *conf, void *zctx, char *sockname)
{
    int err;

    ctx = _zmalloc (sizeof (struct ctx_struct));

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe_all (ctx->zs_in);

    ctx->clients = NULL;
    snprintf (ctx->sockname, sizeof (ctx->sockname), "%s", sockname);

    err = pthread_create (&ctx->t, NULL, _thread, NULL);
    if (err) {
        fprintf (stderr, "apisrv_init: pthread_create: %s\n", strerror (err));
        exit (1);
    }
}

void apisrv_fini (void)
{
    int err;

    err = pthread_join (ctx->t, NULL);
    if (err) {
        fprintf (stderr, "apisrv_fini: pthread_join: %s\n", strerror (err));
        exit (1);
    }
    _zmq_close (ctx->zs_in);
    _zmq_close (ctx->zs_out);
    while (ctx->clients != NULL)
        _client_destroy (ctx->clients);
    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
