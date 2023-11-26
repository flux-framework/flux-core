/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include "src/common/libutil/log.h"

flux_t *h;
flux_future_t *f;

void cancel_cb (int sig)
{
    flux_future_t *f2;
    if (!(f2 = flux_rpc_pack (h,
                              "job-info.update-watch-cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        log_err_exit ("flux_rpc_pack");
    flux_future_destroy (f2);
}

int main (int argc, char *argv[])
{
    flux_jobid_t id;
    const char *key;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc != 3) {
        fprintf (stderr, "Usage: update_watch_stream <jobid> <key>\n");
        exit (1);
    }

    if (flux_job_id_parse (argv[1], &id) < 0)
        log_msg_exit ("error parsing jobid: %s", argv[1]);

    key = argv[2];

    if (!(f = flux_rpc_pack (h,
                             "job-info.update-watch",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:I s:s s:i}",
                             "id", id,
                             "key", key,
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");

    if (signal (SIGUSR1, cancel_cb) == SIG_ERR)
        log_err_exit ("signal");

    while (1) {
        flux_jobid_t check_id;
        json_t *value;
        char *s;

        if (flux_rpc_get_unpack (f, "{s:I}", "id", &check_id) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("job-info.update-watch: id: %s",
                      future_strerror (f, errno));
        }

        if (id != check_id)
            log_msg_exit ("job-info.update-watch returned invalid jobid");

        if (flux_rpc_get_unpack (f, "{s:o}", key, &value) < 0)
            log_msg_exit ("job-info.update-watch: %s: %s",
                          key, future_strerror (f, errno));
        if (!(s = json_dumps (value, 0)))
            log_msg_exit ("invalid json result");
        printf ("%s\n", s);
        fflush (stdout);
        free (s);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
