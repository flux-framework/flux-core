/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* tasyncsock.c - test socket ZMQ_IMMEDIATE, ZMQ_LINGER sockopts */

/* Set up a server that receives a fixed number of messages on a ROUTER
 * socket bound to an ipc:// endpoint, socket with timeout.
 * Set up a client in another thread that connects a DEALER socket to the
 * server and sends one message a fixed number of times.
 * If the correct number of messages is received, silently exit 0.
 * If one of the receives times out meaning some number of messages
 * were lost, allow SIGALRM to terminate the program (exit != 0).
 *
 * If --raw is specified, raw zmq_* functions are used to send the
 * messages; otherwise czmq is used.  zmq_* differs from czmq in its default
 * ZMQ_LINGER setting (zmq_* is -1, while czmq is 0).
 *
 * The two socket options ZMQ_IMMEDIATE and ZMQ_LINGER may be altered
 * in the client using the --immediate=N and --linger=N options.  *
 * Without ZMQ_IMMEDIATE set to 1 (the default), messages may be dropped
 * on a DEALER socket if they manage to be sent before the DEALER has
 * connected to anybody.
 *
 * Without ZMQ_LINGER set to != -1, messages may be dropped on the DEALER
 * socket if they manage to not be sent before the socket is destroyed.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/time.h>
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
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

static char *uri = NULL;
static bool raw = false;
static bool lopt = false;
static int linger = 0;
static bool iopt = false;
static int imm = 0;
static int iter = 0;
static bool vopt = false;
static int bufsize = 0;
static int sleep_usec = 0;

void send_czmq (char *buf, int len)
{
    zctx_t *zctx;
    void *zs;
    zmsg_t *zmsg;

    if (!(zctx = zctx_new ()))
        log_err_exit ("C: zctx_new");
    if (lopt) /* zctx linger default = 0 (flush none) */
        zctx_set_linger (zctx, linger); 
    if (!(zs = zsocket_new (zctx, ZMQ_DEALER)))
        log_err_exit ("C: zsocket_new");
    //if (lopt) // doesn't work here 
    //    zsocket_set_linger (zs, linger); 
    if (iopt)
        zsocket_set_immediate (zs, imm);
    //zsocket_set_sndhwm (zs, 0); /* unlimited */
    if (zsocket_connect (zs, "%s", uri) < 0)
        log_err_exit ("C: zsocket_connect");
    if (!(zmsg = zmsg_new ()))
        oom ();
    if (zmsg_pushmem (zmsg, buf, bufsize) < 0)
        oom ();
    if (zmsg_send (&zmsg, zs) < 0)
        log_err_exit ("C: zmsg_send");
    if (sleep_usec > 0)
        usleep (sleep_usec);
    zctx_destroy (&zctx);
}

void send_raw (char *buf, int len)
{
    void *zctx;
    void *zs;
    int hwm = 0;

    if (!(zctx = zmq_init (1)))
        log_err_exit ("C: zmq_init");
    if (!(zs = zmq_socket (zctx, ZMQ_DEALER)))
        log_err_exit ("C: zmq_socket");
    if (iopt) {
        if (zmq_setsockopt (zs, ZMQ_IMMEDIATE, &imm, sizeof (imm)) < 0)
            log_err_exit ("C: zmq_setsockopt ZMQ_IMMEDIATE %d", imm);
    }
    if (lopt) { /* zmq linger default = -1 (flush all) */
        if (zmq_setsockopt (zs, ZMQ_LINGER, &linger, sizeof (linger)) < 0)
            log_err_exit ("C: zmq_setsockopt ZMQ_LINGER %d", linger);
    }
    if (zmq_setsockopt (zs, ZMQ_SNDHWM, &hwm, sizeof (hwm)) < 0)
        log_err_exit ("C: zmq_setsockopt ZMQ_SNDHWM %d", linger);
    if (zmq_connect (zs, uri) < 0)
        log_err_exit ("C: zmq_connect");
    if (zmq_send (zs, buf, bufsize, 0) < 0)
        log_err_exit ("zmq_send");
    if (sleep_usec > 0)
        usleep (sleep_usec);
    if (zmq_close (zs) < 0)
        log_err_exit ("zmq_close");
#if ZMQ_VERSION_MAJOR < 4
    if (zmq_ctx_destroy (zctx) < 0)
        log_err_exit ("zmq_ctx_destroy");
#else
    if (zmq_ctx_term (zctx) < 0)
        log_err_exit ("zmq_ctx_term");
#endif
}

void *thread (void *arg)
{
    int i;
    char *buf = NULL;

    if (bufsize > 0)
        buf = xzmalloc (bufsize);
    for (i = 0; i < iter; i++) {
        if (vopt)
            log_msg ("sending %d of %d", i + 1, iter);
        if (raw)
            send_raw (buf, bufsize);
        else
            send_czmq (buf, bufsize);
    }
    if (buf)
        free (buf);
    return NULL;
}

#define OPTIONS "rtl:i:vT:s:mS:"
static const struct option longopts[] = {
   {"raw",         no_argument,        0, 'r'},
   {"tcp",         no_argument,        0, 't'},
   {"verbose",     no_argument,        0, 'v'},
   {"monitor",     no_argument,        0, 'm'},
   {"linger",      required_argument,  0, 'l'},
   {"immediate",   required_argument,  0, 'i'},
   {"size",        required_argument,  0, 's'},
   {"timeout",     required_argument,  0, 'T'},
   {"sleep-usec",  required_argument,  0, 'S'},
   {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, 
"Usage: tasyncsock OPTIONS iterations\n"
"       --raw          use zmq_ functions instead of CZMQ\n"
"       --tcp          use tcp transport instead of ipc\n"
"       --linger=N     override default linger (-1=infinite)\n"
"       --immediate=1  set 'immediate' socket option\n"
"       --timeout=N    set receive timeout in seconds (default 1)\n"
"       --size=N       set message payload size (default 0)\n"
"       --monitor      wait for connect event before send\n"
"       --sleep-usec N sleep N usec before closing socket\n"
"       --verbose      be chatty\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    int rc;
    zctx_t *zctx;
    void *zs;
    pthread_t tid;
    pthread_attr_t attr;
    zmsg_t *zmsg;
    int i, ch;
    const char *uritmpl = "ipc://*";
    int timeout_sec = 1;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'r':
                raw = true;
                break;
            case 'l':
                lopt = true;
                linger = strtol (optarg, NULL, 10);
                break;
            case 'i':
                iopt = true;
                imm = strtol (optarg, NULL, 10);
                break;
            case 't':
                uritmpl = "tcp://*:*";
                break;
            case 'T':
                timeout_sec = strtoul (optarg, NULL, 10);
                break;
            case 's':
                bufsize = strtoul (optarg, NULL, 10);
                break;
            case 'v':
                vopt = true;
                break;
            case 'S':
                sleep_usec = strtoul (optarg, NULL, 10);;
                break;
            default:
                usage ();
                /*NOTREACHED*/
        }
    }
    if (optind != argc - 1)
        usage ();
    iter = strtoul (argv[optind++], NULL, 10);

    /* Create socket and bind to it.
     * Store uri in global variable.
     */
    if (!(zctx = zctx_new ()))
        log_err_exit ("S: zctx_new");
    if (!(zs = zsocket_new (zctx, ZMQ_ROUTER)))
        log_err_exit ("S: zsocket_new");
    zsocket_set_rcvhwm (zs, 0); /* unlimited */
    if (zsocket_bind (zs, "%s", uritmpl) < 0)
        log_err_exit ("S: zsocket_bind");
    uri = zsocket_last_endpoint (zs);

    /* Spawn thread which will be our client.
     */
    if ((rc = pthread_attr_init (&attr)))
        log_errn (rc, "S: pthread_attr_init");
    if ((rc = pthread_create (&tid, &attr, thread, NULL)))
        log_errn (rc, "S: pthread_create");

    /* Handle 'iter' client messages with timeout
     */
    for (i = 0; i < iter; i++) {
        alarm (timeout_sec);
        if (!(zmsg = zmsg_recv (zs)))
            log_err_exit ("S: zmsg_recv");
        zmsg_destroy (&zmsg);
        alarm (0);
        if (vopt)
            log_msg ("received message %d of %d", i + 1, iter);
    }

    /* Wait for thread to terminate, then clean up.
     */
    if ((rc = pthread_join (tid, NULL)))
        log_errn (rc, "S: pthread_join");
    zctx_destroy (&zctx); /* destroys sockets too */

    if (strstr (uri, "ipc://"))
        (void)unlink (uri);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
