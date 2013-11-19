/* apicli.c - flux_t implementation for UNIX domain socket */

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
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "cmb.h"
#include "util.h"
#include "flux.h"
#include "handle.h"

#define CMB_CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int rank;
    int size;
    zlist_t *resp;
} cmb_t;

static const struct flux_handle_ops cmb_ops;

static int cmb_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return zmsg_send_fd (c->fd, zmsg);
}

static zmsg_t *cmb_response_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!(z = zlist_pop (c->resp)))
        z = zmsg_recv_fd (c->fd, nonblock);
    return z;
}

static int cmb_response_putmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (zlist_append (c->resp, *zmsg) < 0)
        return -1;
    *zmsg = NULL;
    return 0;
}

/* If 'o' is NULL, there will be no json part,
 *   unlike flux_request_send() which will fill in an empty JSON part.
 */
static int cmb_request_send (void *impl, json_object *o, const char *fmt, ...)
{
    cmb_t *c = impl;
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;

    assert (c->magic == CMB_CTX_MAGIC);
    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);
    zmsg = cmb_msg_encode (tag, o);
    free (tag);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if ((rc = cmb_request_sendmsg (c, &zmsg)) < 0)
        zmsg_destroy (&zmsg);

    return rc;
}

static int cmb_snoop_subscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return cmb_request_send (c, NULL, "api.snoop.subscribe.%s", s ? s: "");
}

static int cmb_snoop_unsubscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return cmb_request_send (c, NULL, "api.snoop.unsubscribe.%s", s ? s: "");
}

static int cmb_event_subscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return cmb_request_send (c, NULL, "api.event.subscribe.%s", s ? s: "");
}

static int cmb_event_unsubscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return cmb_request_send (c, NULL, "api.event.unsubscribe.%s", s ? s: "");
}

static int cmb_event_sendmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    int rc;
    json_object *o = NULL;
    char *tag = NULL;

    assert (c->magic == CMB_CTX_MAGIC);
    if (cmb_msg_decode (*zmsg, &tag, &o) < 0)
        return -1;
    rc = cmb_request_send (c, o, "api.event.send.%s", tag ? tag : "");
    if (rc == 0)
        zmsg_destroy (zmsg);
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
    return rc;
}

static void cmb_fini (void *impl)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (c->fd >= 0)
        (void)close (c->fd);
    free (c);
}

flux_t cmb_init_full (const char *path, int flags)
{
    cmb_t *c = NULL;
    struct sockaddr_un addr;

    c = xzmalloc (sizeof (*c));
    if (!(c->resp = zlist_new ()))
        oom ();
    c->magic = CMB_CTX_MAGIC;
    c->fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0)
        goto error;
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (connect (c->fd, (struct sockaddr *)&addr,
                         sizeof (struct sockaddr_un)) < 0)
        goto error;
    return flux_handle_create (c, &cmb_ops, flags);
error:
    if (c)
        cmb_fini (c);
    return NULL;
}

flux_t cmb_init (void)
{
    const char *val;
    char path[PATH_MAX + 1];

    if ((val = getenv ("CMB_API_PATH"))) {
        if (strlen (val) > PATH_MAX) {
            err ("Crazy value for CMB_API_PATH!");
            return (NULL);
        }
        strcpy(path, val);
    }
    else
        snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());
    return cmb_init_full (path, 0);
}

static const struct flux_handle_ops cmb_ops = {
    .request_sendmsg = cmb_request_sendmsg,
    .response_recvmsg = cmb_response_recvmsg,
    .response_putmsg = cmb_response_putmsg,
    .event_sendmsg = cmb_event_sendmsg,
    .event_recvmsg = cmb_response_recvmsg, /* FIXME */
    .event_subscribe = cmb_event_subscribe,
    .event_unsubscribe = cmb_event_unsubscribe,
    .snoop_recvmsg = cmb_response_recvmsg, /* FIXME */
    .snoop_subscribe = cmb_snoop_subscribe,
    .snoop_unsubscribe = cmb_snoop_unsubscribe,
    .impl_destroy = cmb_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
