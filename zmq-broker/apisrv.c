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

static char *_strdup (char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        _oom ();
    return cpy;
}

static void _client_create (int fd)
{
    client_t *c;

    c = _zmalloc (sizeof (client_t));
    c->fd = fd;
    c->prev = NULL;
    c->next = ctx->clients;
    if (c->next)
        c->next->prev = c;
    ctx->clients = c;
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
    if (strlen (c->uuid) > 0)
        _zmq_2part_send_json (ctx->zs_out, NULL, "event.%s.disconnect",c->uuid);
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
    int fd;

    fd = accept (ctx->listen_fd, NULL, NULL); 
    if (fd < 0) {
        fprintf (stderr, "apisrv: accept: %s\n", strerror (errno));
        exit (1);
    }
     _client_create (fd);
}

static int _client_read (client_t *c)
{
    const char *api_subscribe = "api.subscribe.";
    const char *api_setuuid = "api.setuuid.";
    int bodylen, taglen, totlen;
    char *tag, *body;

    totlen = recv (c->fd, ctx->buf, sizeof (ctx->buf), MSG_DONTWAIT);
    if (totlen < 0) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK)
            fprintf (stderr, "apisrv: API read: %s\n", strerror (errno));
        return -1;
    }
    if (totlen == 0) /* EOF */
        return -1;

    for (taglen = 0; taglen < totlen; taglen++) {
        if (ctx->buf[taglen] == '\0')
            break;
    }
    if (taglen == totlen)
	return -1;
    tag = _strdup (ctx->buf);
    body = &ctx->buf[taglen + 1]; /* not null terminated */
    bodylen = totlen - taglen - 1;

    if (!strcmp (tag, "api.unsubscribe")) {
        if (c->subscription) {
            free (c->subscription);
            c->subscription = NULL;
        }
    } else if (!strncmp (tag, api_subscribe, strlen (api_subscribe))) {
        char *p = tag + strlen (api_subscribe);
        if (c->subscription)
            free (c->subscription);
        c->subscription = _strdup (p);
    } else if (!strncmp (tag, api_setuuid, strlen (api_setuuid))) {
        char *p = tag + strlen (api_setuuid);
        snprintf (c->uuid, sizeof (c->uuid), "%s", p);
        _zmq_2part_send_json (ctx->zs_out, NULL, "event.%s.connect", c->uuid);
    } else
        _zmq_2part_send_buf (ctx->zs_out, body, bodylen, "%s", tag);
    free (tag);

    return 0;
}

static void _readmsg (bool *shutdownp)
{
    zmq_2part_t msg;
    client_t *c, *deleteme;
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
    for (c = ctx->clients; c != NULL; ) {
        deleteme = NULL;
        if (c->subscription && _zmq_2part_match (&msg, c->subscription)) {
            n = send (c->fd, ctx->buf, len, 0);
            if (n < len)
                deleteme = c; 
        }
        c = c->next;
        if (deleteme)
            _client_destroy (deleteme);
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
            while (_client_read (c) != -1)
                ; /* there may be multiple messages queued */
            if (errno != EWOULDBLOCK)
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
