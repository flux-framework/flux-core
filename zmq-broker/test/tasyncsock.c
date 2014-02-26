/* tasyncsock.c - test ZMQ_IMMEDIATE */

/* Without the ZMQ_IMMEDIATE socket option, a message sent immediately
 * after the first connect on a dealer socket may be lost.
 */

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

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

static char *uri = NULL;

const bool use_raw = 0;

void *thread (void *arg)
{
    char buf[1];

    memset (buf, 0, sizeof (buf));
    if (use_raw) {
        void *zctx;
        void *zs;
        int i;

        if (!(zctx = zmq_init (1)))
            err_exit ("C: zmq_init");
        if (!(zs = zmq_socket (zctx, ZMQ_DEALER)))
            err_exit ("C: zmq_socket");

        i = 1;
        if (zmq_setsockopt (zs, ZMQ_IMMEDIATE, &i, sizeof (i)) < 0)
            err_exit ("C: zmq_setsockopt");

        msg ("C: connect");
        if (zmq_connect (zs, uri) < 0)
            err_exit ("C: zmq_connect");

        msg ("C: send");
        if (zmq_send (zs, buf, sizeof (buf), 0) < 0)
            err_exit ("zmq_send");
       
        msg ("C: done"); 
        zmq_term (&zctx);
    } else {
        zctx_t *zctx;
        void *zs;
        zmsg_t *zmsg;

        if (!(zctx = zctx_new ()))
            err_exit ("C: zctx_new");
        zctx_set_linger (zctx, -1); /* restore zmq -1 default linger value */
        if (!(zs = zsocket_new (zctx, ZMQ_DEALER)))
            err_exit ("C: zsocket_new");
        zsocket_set_immediate (zs, 1);

        msg ("C: connect %s", uri);
        if (zsocket_connect (zs, uri) < 0)
            err_exit ("C: zsocket_connect");

        msg ("C: send");
        if (!(zmsg = zmsg_new ()))
            oom ();
        if (zmsg_pushmem (zmsg, buf, sizeof (buf)) < 0)
            oom ();
        if (zmsg_send (&zmsg, zs) < 0)
            err_exit ("C: zmsg_send");

        msg ("C: done");
        zctx_destroy (&zctx);
    }

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

    log_init (basename (argv[0]));

    if (argc != 1) {
        fprintf (stderr, "Usage: tasyncsock\n");
        exit (1);
    }

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
    zmsg_dump (zmsg);

    /* Wait for thread to terminate, then clean up.
     */
    msg ("S: pthread_join");
    if ((rc = pthread_join (tid, NULL)))
        errn (rc, "S: pthread_join");
    zctx_destroy (&zctx); /* destroys sockets too */
    msg ("S: done");

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
