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

/* tmunge.c - test MUNGE wrapper */

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

#if CZMQ_VERSION_MAJOR < 2
#define zmsg_pushstrf zmsg_pushstr
#endif

static const char *uri = "inproc://tmunge";
static int nframes;
static void *cs;

void *thread (void *arg)
{
    zmsg_t *zmsg;
    flux_sec_t *sec;
    int i;

    if (!(sec = flux_sec_create ()))
        log_err_exit ("C: flux_sec_create");
    if (flux_sec_disable (sec, FLUX_SEC_TYPE_ALL) < 0)
        log_err_exit ("C: flux_sec_disable ALL");
    if (flux_sec_enable (sec, FLUX_SEC_TYPE_MUNGE) < 0)
        log_err_exit ("C: flux_sec_enable MUNGE");
    if (flux_sec_munge_init (sec) < 0)
        log_err_exit ("C: flux_sec_munge_init: %s", flux_sec_errstr (sec));

    if (!(zmsg = zmsg_new ()))
        oom ();
    for (i = nframes - 1; i >= 0; i--)
        if (zmsg_pushstrf (zmsg, "frame.%d", i) < 0)
            oom ();
    //zmsg_dump (zmsg);
    if (flux_sec_munge_zmsg (sec, &zmsg) < 0)
        log_err_exit ("C: flux_sec_munge_zmsg: %s", flux_sec_errstr (sec));
    //zmsg_dump (zmsg);
    if (zmsg_send (&zmsg, cs) < 0)
        log_err_exit ("C: zmsg_send");

    flux_sec_destroy (sec);

    return NULL;
}

int main (int argc, char *argv[])
{
    int rc;
    void *zs;
    pthread_t tid;
    pthread_attr_t attr;
    zmsg_t *zmsg;
    flux_sec_t *sec;
    int n;
    zctx_t *zctx;

    log_init (basename (argv[0]));

    if (argc != 2) {
        fprintf (stderr, "Usage: tmunge nframes\n");
        exit (1);
    }
    nframes = strtoul (argv[1], NULL, 10);

    if (!(sec = flux_sec_create ()))
        log_err_exit ("flux_sec_create");
    if (flux_sec_disable (sec, FLUX_SEC_TYPE_ALL) < 0)
        log_err_exit ("flux_sec_disable ALL");
    if (flux_sec_enable (sec, FLUX_SEC_TYPE_MUNGE) < 0)
        log_err_exit ("flux_sec_enable MUNGE");
    if (flux_sec_munge_init (sec) < 0)
        log_err_exit ("flux_sec_munge_init: %s", flux_sec_errstr (sec));

    if (!(zctx = zctx_new ()))
        log_err_exit ("S: zctx_new");
    if (!(zs = zsocket_new (zctx, ZMQ_SUB)))
        log_err_exit ("S: zsocket_new");
    if (zsocket_bind (zs, "%s", uri) < 0)
        log_err_exit ("S: zsocket_bind");
    zsocket_set_subscribe (zs, "");

    if (!(cs = zsocket_new (zctx, ZMQ_PUB)))
        log_err_exit ("S: zsocket_new");
    if (zsocket_connect (cs, "%s", uri) < 0)
        log_err_exit ("S: zsocket_connect");

    if ((rc = pthread_attr_init (&attr)))
        log_errn (rc, "S: pthread_attr_init");
    if ((rc = pthread_create (&tid, &attr, thread, NULL)))
        log_errn (rc, "S: pthread_create");

    /* Handle one client message.
     */
    if (!(zmsg = zmsg_recv (zs)))
        log_err_exit ("S: zmsg_recv");
    //zmsg_dump (zmsg);
    if (flux_sec_unmunge_zmsg (sec, &zmsg) < 0)
        log_err_exit ("S: flux_sec_unmunge_zmsg: %s", flux_sec_errstr (sec));
    //zmsg_dump (zmsg);
    if ((n = zmsg_size (zmsg) != nframes))
        log_msg_exit ("S: expected %d frames, got %d", nframes, n);

    /* Wait for thread to terminate, then clean up.
     */
    if ((rc = pthread_join (tid, NULL)))
        log_errn (rc, "S: pthread_join");
    zctx_destroy (&zctx); /* destroys sockets too */

    flux_sec_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
