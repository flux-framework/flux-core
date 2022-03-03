/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <signal.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/log.h"

flux_t *h;
flux_future_t *f;

void cancel_cb (int sig)
{
    flux_future_t *f2;
    if (!(f2 = flux_rpc_pack (h,
                              "job-manager.events-journal-cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        log_err_exit ("flux_rpc_pack");
    flux_future_destroy (f2);
}

int main (int argc, char *argv[])
{
    ssize_t inlen;
    void *inbuf;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc != 1) {
        fprintf (stderr, "Usage: events_journal_stream <payload\n");
        exit (1);
    }

    if ((inlen = read_all (STDIN_FILENO, &inbuf)) < 0)
        log_err_exit ("read from stdin");
    if (inlen > 0)  // flux stringified JSON payloads are sent with \0-term
        inlen++;    //  and read_all() ensures inbuf has one, not acct in inlen

    if (!(f = flux_rpc_raw (h,
                            "job-manager.events-journal",
                            inbuf,
                            inlen,
                            FLUX_NODEID_ANY,
                            FLUX_RPC_STREAMING)))
        log_err_exit ("flux_rpc_raw");

    if (signal (SIGUSR1, cancel_cb) == SIG_ERR)
        log_err_exit ("signal");

    while (1) {
        flux_jobid_t id;
        json_t *events;
        size_t index;
        json_t *entry;
        if (flux_rpc_get_unpack (f,
                                 "{s:I s:o}",
                                 "id", &id,
                                 "events", &events) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("job-manager.events-journal: %s",
                          future_strerror (f, errno));
        }
        json_array_foreach (events, index, entry) {
            /* For testing, wrap each eventlog entry in an outer object that
             * includes the jobid.  Not coincidentally, this looks like
             * the old format for job manager journal entries.
             */
            json_t *o;
            char *s;

            if (!(o = json_pack ("{s:I s:O}",
                                 "id", id,
                                 "entry", entry))
                || !(s = json_dumps (o, 0)))
                log_msg_exit ("Error creating eventlog envelope");
            printf ("%s\n", s);
            fflush (stdout);
            free (s);
            json_decref (o);
        }
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    free (inbuf);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
