/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job stats */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"

int cmd_stats (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *topic = "job-list.job-stats";
    const char *s;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc (h, topic, NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get (f, &s) < 0)
        log_msg_exit ("stats: %s", future_strerror (f, errno));

    /* for time being, just output json object for result */
    printf ("%s\n", s);
    flux_close (h);
    return (0);
}

/* vi: ts=4 sw=4 expandtab
 */
