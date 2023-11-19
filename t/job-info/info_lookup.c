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
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;
    flux_jobid_t id;
    json_t *keys;
    int i;

    if (argc < 3) {
        fprintf (stderr, "Usage: info_lookup <jobid> <key> ...\n");
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (flux_job_id_parse (argv[1], &id) < 0)
        log_msg_exit ("error parsing jobid: %s", argv[1]);

    if (!(keys = json_array ()))
        log_msg_exit ("json_array");

    for (i = 2; i < argc; i++) {
        json_t *key = json_string (argv[i]);
        if (!key)
            log_msg_exit ("json_string_value");
        if (json_array_append_new (keys, key) < 0)
            log_msg_exit ("json_array_append_new");
    }

    if (!(f = flux_rpc_pack (h,
                             "job-info.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:O s:i}",
                             "id", id,
                             "keys", keys,
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");

    for (i = 2; i < argc; i++) {
        json_t *value;
        char *s;
        if (flux_rpc_get_unpack (f, "{s:o}", argv[i], &value) < 0)
            log_msg_exit ("job-info.lookup: %s",
                          future_strerror (f, errno));
        if (!(s = json_dumps (value, JSON_ENCODE_ANY)))
            log_msg_exit ("invalid json result");
        printf ("%s\n", s);
        fflush (stdout);
        free (s);
    }

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
