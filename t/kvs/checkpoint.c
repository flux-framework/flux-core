/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <flux/core.h>
#include "src/common/libutil/log.h"

void usage (void)
{
    fprintf (stderr, "Usage: checkpoint get key\n"
                     "   or: checkpoint put key value\n");
    exit (1);
}

int main (int argc, char **argv)
{
    flux_t *h;
    const char *cmd;
    const char *key;
    const char *value;
    flux_future_t *f;

    if (argc < 2 || argc > 4)
        usage ();
    cmd = argv[1];
    key = argv[2];
    if (argc == 4)
        value = argv[3];
    if (strcmp (cmd, "get") != 0 && strcmp (cmd, "put") != 0)
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!strcmp (cmd, "put")) {
        if (!(f = flux_rpc_pack (h,
                                 "kvs-checkpoint.put",
                                 0,
                                 0,
                                 "{s:s s:s}",
                                 "key",
                                 key,
                                 "value",
                                 value)))
            log_err_exit("flux_rpc");
        if (flux_rpc_get (f, NULL) < 0)
            log_err_exit ("%s", key);
    }
    else {
        if (!(f = flux_rpc_pack (h,
                                 "kvs-checkpoint.get",
                                 0,
                                 0,
                                 "{s:s}",
                                 "key",
                                 key)))
            log_err_exit("flux_rpc");
        if (flux_rpc_get_unpack (f, "{s:s}", "value", &value) < 0)
            log_err_exit ("%s", key);
        printf ("%s\n", value);
    }

    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
