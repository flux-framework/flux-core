/* tmunge.c - test MUNGE wrapper */

#include <sys/types.h>
#include <sys/time.h>
#include <json/json.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <czmq.h>
#include <libgen.h>
#include <pthread.h>

#include "flux.h"
#include "util.h"
#include "log.h"
#include "zmsg.h"
#include "security.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

static char *uri = NULL;

void *thread (void *arg)
{
    zctx_t *zctx;
    void *zs;
    zmsg_t *zmsg;
    flux_sec_t sec;

    if (!(sec = flux_sec_create ()))
        err_exit ("C: flux_sec_create");
    if (flux_sec_disable (sec, FLUX_SEC_TYPE_ALL) < 0)
        err_exit ("C: flux_sec_disable ALL");
    if (flux_sec_enable (sec, FLUX_SEC_TYPE_MUNGE) < 0)
        err_exit ("C: flux_sec_enable MUNGE");
    if (flux_sec_munge_init (sec) < 0)
        err_exit ("C: flux_sec_munge_init: %s", flux_sec_errstr (sec));

    if (!(zctx = zctx_new ()))
        err_exit ("C: zctx_new");
    zctx_set_linger (zctx, -1); /* restore zmq -1 default linger value */
    if (!(zs = zsocket_new (zctx, ZMQ_DEALER)))
        err_exit ("C: zsocket_new");
    zsocket_set_immediate (zs, 1);

    msg ("C: connect %s", uri);
    if (zsocket_connect (zs, uri) < 0)
        err_exit ("C: zsocket_connect");

    msg ("C: create");
    if (!(zmsg = zmsg_new ()))
        oom ();
    if (zmsg_pushstr (zmsg, "frame.3") < 0)
        oom ();
    if (zmsg_pushstr (zmsg, "frame.2") < 0)
        oom ();
    if (zmsg_pushstr (zmsg, "frame.1") < 0)
        oom ();
    zmsg_dump (zmsg);
    msg ("C: munge");
    if (flux_sec_munge_zmsg (sec, &zmsg) < 0)
        err_exit ("C: flux_sec_munge_zmsg: %s", flux_sec_errstr (sec));
    zmsg_dump (zmsg);
    msg ("C: send");
    if (zmsg_send (&zmsg, zs) < 0)
        err_exit ("C: zmsg_send");

    msg ("C: done");
    zctx_destroy (&zctx);

    flux_sec_destroy (sec);

    return NULL;
}

int main (int argc, char *argv[])
{
    int rc;
    zctx_t *zctx;
    void *zs;
    pthread_t tid;
    pthread_attr_t attr;
    zmsg_t *zmsg;
    flux_sec_t sec;
    zframe_t *zf;

    log_init (basename (argv[0]));

    if (argc != 1) {
        fprintf (stderr, "Usage: tasyncsock\n");
        exit (1);
    }

    if (!(sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    if (flux_sec_disable (sec, FLUX_SEC_TYPE_ALL) < 0)
        err_exit ("flux_sec_disable ALL");
    if (flux_sec_enable (sec, FLUX_SEC_TYPE_MUNGE) < 0)
        err_exit ("flux_sec_enable MUNGE");
    if (flux_sec_munge_init (sec) < 0)
        err_exit ("flux_sec_munge_init: %s", flux_sec_errstr (sec));

    /* Create socket and bind to it.
     * Store uri in global variable.
     */
    if (!(zctx = zctx_new ()))
        err_exit ("S: zctx_new");
    if (!(zs = zsocket_new (zctx, ZMQ_ROUTER)))
        err_exit ("S: zsocket_new");
    if (zsocket_bind (zs, "ipc://*") < 0)
        err_exit ("S: zsocket_bind");
    uri = zsocket_last_endpoint (zs);
    msg ("S: bind %s", uri);

    /* Spawn thread which will be our client.
     */
    msg ("S: start client");
    if ((rc = pthread_attr_init (&attr)))
        errn (rc, "S: pthread_attr_init");
    if ((rc = pthread_create (&tid, &attr, thread, NULL)))
        errn (rc, "S: pthread_create");

    /* Handle one client message.
     */
    msg ("S: recv");
    if (!(zmsg = zmsg_recv (zs)))
        err_exit ("S: zmsg_recv");
    if ((zf = zmsg_pop (zmsg))) /* drop uuid */
        zframe_destroy (&zf);
    zmsg_dump (zmsg);
    msg ("S: unmunge");
    if (flux_sec_unmunge_zmsg (sec, &zmsg) < 0)
        err_exit ("S: flux_sec_unmunge_zmsg: %s", flux_sec_errstr (sec));
    zmsg_dump (zmsg);

    /* Wait for thread to terminate, then clean up.
     */
    msg ("S: pthread_join");
    if ((rc = pthread_join (tid, NULL)))
        errn (rc, "S: pthread_join");
    zctx_destroy (&zctx); /* destroys sockets too */
    msg ("S: done");

    flux_sec_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
