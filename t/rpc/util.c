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
    flux_msg_handler_t *w;
    test_server_f cb;
    void *arg;
    pthread_t thread;
    int rc;
    zuuid_t *uuid;
};

void shutdown_cb (flux_t *h, flux_msg_handler_t *w,
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
        flux_msg_handler_destroy (a->w);
        flux_close (a->s);
        zuuid_destroy (&a->uuid);
        free (a);
    }
}

flux_t *test_server_create (test_server_f cb, void *arg)
{
    int e;
    struct test_server *a;
    char uri[128];

    if (!(a = calloc (1, sizeof (*a)))) {
        errno = ENOMEM;
        goto error;
    }
    if (!cb) {
        errno = EINVAL;
        goto error;
    }
    a->cb = cb;
    a->arg = arg;

    if (!(a->uuid = zuuid_new ())) {
        errno = ENOMEM;
        goto error;
    }

    /* Create back-to-back wired flux_t handles
     */
    (void)setenvf ("FLUX_HANDLE_ROLEMASK", 1, "0x%x", 1);
    (void)setenvf ("FLUX_HANDLE_USERID", 1, "%d", geteuid());
    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    snprintf (uri, sizeof (uri), "shmem://%s&bind", zuuid_str (a->uuid));
    if (!(a->s = flux_open (uri, 0))) {
        diag ("%s: flux_open %s: %s\n",
              __FUNCTION__, uri, flux_strerror (errno));
        goto error;
    }
    snprintf (uri, sizeof (uri), "shmem://%s&connect", zuuid_str (a->uuid));
    if (!(a->c = flux_open (uri, 0))) {
        diag ("%s: flux_open %s: %s\n",
              __FUNCTION__, uri, flux_strerror (errno));
        goto error;
    }

    /* Register watcher for "shutdown" request on server side
     */
    struct flux_match match = FLUX_MATCH_REQUEST;
    match.topic_glob = "shutdown";
    if (!(a->w = flux_msg_handler_create (a->s, match, shutdown_cb, a))) {
        diag ("%s: flux_msg_handler_create: %s\n",
             __FUNCTION__, flux_strerror (errno));
        goto error;
    }
    flux_msg_handler_start (a->w);

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
