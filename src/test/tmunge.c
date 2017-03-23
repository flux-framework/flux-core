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
static zsock_t *cs;
int sec_typemask = FLUX_SEC_TYPE_MUNGE;

void *thread (void *arg)
{
    flux_msg_t *msg;
    flux_sec_t *sec;
    int n;

    if (!(sec = flux_sec_create (sec_typemask, NULL)))
        log_err_exit ("C: flux_sec_create");
    if (flux_sec_comms_init (sec) < 0)
        log_err_exit ("C: flux_sec_comms_init: %s", flux_sec_errstr (sec));
    if (!(msg = flux_event_encode ("foo.topic", "{\"foo\":42}")))
        log_err_exit ("C: flux_event_encode");
    if ((n = flux_msg_frames (msg)) != 4)
        log_err_exit ("C: expected 4 frames, got %d", n);
    if (flux_msg_sendzsock_munge (cs, msg, sec) < 0)
        log_err_exit ("C: flux_msg_sendzsock_munge");
    flux_msg_destroy (msg);
    flux_sec_destroy (sec);

    return NULL;
}

int main (int argc, char *argv[])
{
    int rc;
    zsock_t *zs;
    pthread_t tid;
    pthread_attr_t attr;
    flux_msg_t *msg;
    flux_sec_t *sec;
    int n;

    log_init (basename (argv[0]));

    if (argc != 1 && argc != 2) {
        fprintf (stderr, "Usage: tmunge [--fake]\n");
        exit (1);
    }
    if (argc == 2) {
        if (!strcmp (argv[1], "--fake"))
            sec_typemask |= FLUX_SEC_FAKEMUNGE;
        else
            log_msg_exit ("unknown option %s", argv[1]);
    }
    if (!(sec = flux_sec_create (sec_typemask, NULL)))
        log_err_exit ("flux_sec_create");
    if (flux_sec_comms_init (sec) < 0)
        log_err_exit ("flux_sec_comms_init: %s", flux_sec_errstr (sec));

    if (!(zs = zsock_new_sub (uri, "")))
        log_err_exit ("S: zsock_new_sub");

    if (!(cs = zsock_new_pub (uri)))
        log_err_exit ("S: zsock_new_pub");

    if ((rc = pthread_attr_init (&attr)))
        log_errn (rc, "S: pthread_attr_init");
    if ((rc = pthread_create (&tid, &attr, thread, NULL)))
        log_errn (rc, "S: pthread_create");

    /* Handle one client message.
     */
    if (!(msg = flux_msg_recvzsock_munge (zs, sec)))
        log_err_exit ("S: flux_msg_recvzsock_munge: %s", flux_sec_errstr (sec));
    //zmsg_dump (zmsg);
    if ((n = flux_msg_frames (msg)) != 4)
        log_err_exit ("S: expected 4 frames, got %d", n);
    flux_msg_destroy (msg);

    /* Wait for thread to terminate, then clean up.
     */
    if ((rc = pthread_join (tid, NULL)))
        log_errn (rc, "S: pthread_join");

    zsock_destroy (&zs);
    zsock_destroy (&cs);
    flux_sec_destroy (sec);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
