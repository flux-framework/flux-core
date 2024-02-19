/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job urgency */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "common.h"

struct optparse_option urgency_opts[] =  {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Output old urgency value on success",
    },
    OPTPARSE_TABLE_END
};

int cmd_urgency (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f;
    int urgency, old_urgency;
    flux_jobid_t id;
    const char *jobid = NULL;
    const char *urgencystr = NULL;

    if (optindex != argc - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    jobid = argv[optindex++];
    id = parse_jobid (jobid);
    urgencystr = argv[optindex++];
    if (!strcasecmp (urgencystr, "hold"))
        urgency = FLUX_JOB_URGENCY_HOLD;
    else if (!strcasecmp (urgencystr, "expedite"))
        urgency = FLUX_JOB_URGENCY_EXPEDITE;
    else if (!strcasecmp (urgencystr, "default"))
        urgency = FLUX_JOB_URGENCY_DEFAULT;
    else
        urgency = parse_arg_unsigned (urgencystr, "urgency");

    if (!(f = flux_job_set_urgency (h, id, urgency)))
        log_err_exit ("flux_job_set_urgency");
    if (flux_rpc_get_unpack (f, "{s:i}", "old_urgency", &old_urgency) < 0)
        log_msg_exit ("%s: %s", jobid, future_strerror (f, errno));
    if (optparse_hasopt (p, "verbose")) {
        if (old_urgency == FLUX_JOB_URGENCY_HOLD)
            fprintf (stderr, "old urgency: job held\n");
        else if (old_urgency == FLUX_JOB_URGENCY_EXPEDITE)
            fprintf (stderr, "old urgency: job expedited\n");
        else
            fprintf (stderr, "old urgency: %d\n", old_urgency);
    }
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
