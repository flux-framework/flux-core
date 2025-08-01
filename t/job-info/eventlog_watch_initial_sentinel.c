/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <jansson.h>
#include <stdio.h>
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

flux_t *h;
flux_future_t *f;

#define OPTIONS "W"
static const struct option longopts[] = {
   {"waitcreate", no_argument, 0, 'W'},
   {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: eventlog_watch_initial_sentinel <jobid> <path>\n");
    exit (1);
}


int main (int argc, char *argv[])
{
    flux_jobid_t id;
    const char *jobid;
    const char *path;
    int flags = 0;
    int Wopt = 0;
    int ch;

    log_init (argv[0]);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'W':
                Wopt = true;
                break;
            default:
                usage ();
        }
    }

    if ((argc - optind) != 2)
        usage();

    jobid = argv[optind++];
    path = argv[optind++];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (flux_job_id_parse (jobid, &id) < 0)
        log_msg_exit ("error parsing jobid: %s", argv[1]);

    flags = FLUX_JOB_EVENT_WATCH_INITIAL_SENTINEL;
    if (Wopt)
        flags |= FLUX_JOB_EVENT_WATCH_WAITCREATE;

    if (!(f = flux_rpc_pack (h,
                             "job-info.eventlog-watch",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:I s:s s:i}",
                             "id", id,
                             "path", path,
                             "flags", flags)))
        log_err_exit ("flux_rpc_pack");

    while (1) {
        const char *event;
        if (flux_job_event_watch_get (f, &event) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("job-info.eventlog-watch: %s",
                          future_strerror (f, errno));
        }
        if (!event)
            printf ("sentinel\n");
        else
            printf ("%s", event);
        fflush (stdout);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
