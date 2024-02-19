/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job purge */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "common.h"

struct optparse_option purge_opts[] = {
    { .name = "age-limit", .has_arg = 1, .arginfo = "FSD",
      .usage = "Purge jobs that became inactive beyond age-limit.",
    },
    { .name = "num-limit", .has_arg = 1, .arginfo = "COUNT",
      .usage = "Purge oldest inactive jobs until COUNT are left.",
    },
    { .name = "force", .key = 'f', .has_arg = 0,
      .usage = "Perform the irreversible purge.",
    },
    { .name = "batch", .has_arg = 1, .arginfo = "COUNT",
      .usage = "Limit number of jobs per request (default 50).",
    },
    OPTPARSE_TABLE_END
};

static void purge_finish (flux_t *h, int force, int total)
{
    int inactives;
    flux_future_t *f;

    if (!(f = flux_rpc (h, "job-manager.stats-get", NULL, 0, 0))
        || flux_rpc_get_unpack (f, "{s:i}", "inactive_jobs", &inactives) < 0)
        log_err_exit ("purge: failed to fetch inactive job count");
    flux_future_destroy (f);

    if (force)
        printf ("purged %d inactive jobs, %d remaining\n", total, inactives);
    else {
        printf ("use --force to purge %d of %d inactive jobs\n",
                total,
                inactives);
    }
}

static int purge_range (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    double age_limit = optparse_get_duration (p, "age-limit", -1.);
    int num_limit = optparse_get_int (p, "num-limit", -1);
    int batch = optparse_get_int (p, "batch", 50);
    int force = 0;
    int count;
    int total = 0;

    if (optparse_hasopt (p, "force"))
        force = 1;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    do {
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-manager.purge",
                                 0,
                                 0,
                                 "{s:f s:i s:i s:b}",
                                 "age_limit", age_limit,
                                 "num_limit", num_limit,
                                 "batch", batch,
                                 "force", force))
            || flux_rpc_get_unpack (f, "{s:i}", "count", &count) < 0)
            log_msg_exit ("purge: %s", future_strerror (f, errno));
        total += count;
        flux_future_destroy (f);
    } while (force && count == batch);

    purge_finish (h, force, total);
    flux_close (h);
    return 0;
}

static void purge_id_continuation (flux_future_t *f, void *arg)
{
    int *count = arg;
    int tmp;
    if (flux_rpc_get_unpack (f, "{s:i}", "count", &tmp) < 0)
        log_msg_exit ("purge: %s", future_strerror (f, errno));
    (*count) += tmp;
    flux_future_destroy (f);
}

static int purge_ids (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    int force = 0;
    int total = 0;
    int i, ids_len;

    if (optparse_hasopt (p, "force"))
        force = 1;
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    ids_len = argc - optindex;
    for (i = 0; i < ids_len; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex + i]);
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-manager.purge-id",
                                 0,
                                 0,
                                 "{s:I s:i}",
                                 "id", id,
                                 "force", force)))
            log_err_exit ("job-manager.purge-id");
        if (flux_future_then (f, -1, purge_id_continuation, &total) < 0)
            log_err_exit ("flux_future_then");
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    purge_finish (h, force, total);
    flux_close (h);
    return 0;
}

int cmd_purge (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    if ((argc - optindex) > 0)
        return purge_ids (p, argc, argv);
    return purge_range (p, argc, argv);
}

/* vi: ts=4 sw=4 expandtab
 */
