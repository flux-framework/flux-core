/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Note that the job-info.update-lookup RPC target is deprecated.
 * This is to test legacy behavior.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <jansson.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;
    flux_jobid_t id;
    const char *key;
    json_t *value;
    char *s;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (argc != 3) {
        fprintf (stderr, "Usage: update_lookup <jobid> <key>\n");
        exit (1);
    }

    if (flux_job_id_parse (argv[1], &id) < 0)
        log_msg_exit ("error parsing jobid: %s", argv[1]);

    key = argv[2];

    if (!(f = flux_rpc_pack (h,
                             "job-info.update-lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:s s:i}",
                             "id", id,
                             "key", key,
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");

    if (flux_rpc_get_unpack (f, "{s:o}", key, &value) < 0)
        log_msg_exit ("job-info.update-lookup: %s",
                      future_strerror (f, errno));
    if (!(s = json_dumps (value, 0)))
        log_msg_exit ("invalid json result");
    printf ("%s\n", s);
    fflush (stdout);
    free (s);

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
