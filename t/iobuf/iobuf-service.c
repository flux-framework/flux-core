/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* iobuf-service.c - create/destroy iobuf service */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libiobuf/iobuf.h"
#include "src/common/libutil/log.h"

void usage (void)
{
    fprintf (stderr,
"Usage: iobuf-service <name> <maxbuffers> <eofcount>\n"
);
    exit (1);
}

void sig_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    int sig = flux_signal_watcher_get_signum (w);

    if (sig == SIGTERM)
        flux_reactor_stop (r);
}

void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    printf ("reactor ready\n");
    fflush (stdout);
    flux_watcher_stop (w);
}

void eof_count_cb (iobuf_t *iob, void *arg)
{
    printf ("eof max reached\n");
    fflush (stdout);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    char *name = NULL;
    int maxbuffers;
    int eofcount;
    iobuf_t *iob = NULL;
    flux_future_t *f = NULL;
    flux_watcher_t *sigtermw = NULL;
    flux_watcher_t *tw = NULL;

    log_init ("iobuf-service");

    if (argc != 4)
        usage ();

    name = argv[1];
    maxbuffers = atoi (argv[2]);
    if (maxbuffers < 0)
        log_err_exit ("invalid maxbuffers");
    eofcount = atoi (argv[3]);
    if (eofcount < 0)
        log_err_exit ("invalid eofcount");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(iob = iobuf_server_create (h,
                                     name,
                                     maxbuffers,
                                     IOBUF_FLAG_LOG_ERRORS)))
        log_err_exit ("iobuf_server_create");

    if (!(f = flux_service_register (h, name)))
        log_err_exit ("flux_service_register");

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    if (eofcount) {
        if (iobuf_set_eof_count_cb (iob,
                                    eofcount,
                                    eof_count_cb,
                                    NULL) < 0)
            log_err_exit ("iobuf_set_eof_count_cb");
    }

    if (!(sigtermw = flux_signal_watcher_create (flux_get_reactor (h),
                                          SIGTERM,
                                          sig_cb,
                                          iob)))
        log_err_exit ("flux_signal_watcher_create");

    flux_watcher_start (sigtermw);

    /* the timer watcher is only for syncing with tests that
     * background this service */
    if (!(tw = flux_timer_watcher_create (flux_get_reactor (h),
                                          0., 0.,
                                          timer_cb,
                                          iob)))
        log_err_exit ("flux_timer_watcher_create");

    flux_watcher_start (tw);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (!(f = flux_service_unregister (h, name)))
        log_err_exit ("flux_service_unregister");

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    flux_watcher_destroy (sigtermw);
    flux_watcher_destroy (tw);
    flux_future_destroy (f);
    iobuf_server_destroy (iob);
    flux_close (h);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
