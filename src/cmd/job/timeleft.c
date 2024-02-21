/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job timeleft */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <limits.h>
#include <math.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fsd.h"
#include "common.h"

struct optparse_option timeleft_opts[] = {
    { .name = "human", .key = 'H', .has_arg = 0,
      .usage = "Output in Flux Standard Duration instead of seconds.",
    },
    OPTPARSE_TABLE_END
};

int cmd_timeleft (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_error_t error;
    double t;

    if (optindex < argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optindex < argc) {
        if (setenv ("FLUX_JOB_ID", argv[optindex++], 1) < 0)
            log_err_exit ("setenv");
    }
    if (flux_job_timeleft (h, &error, &t) < 0)
        log_msg_exit ("%s", error.text);
    if (optparse_hasopt (p, "human")) {
        char buf[64];
        if (fsd_format_duration (buf, sizeof (buf), t) < 0)
            log_err_exit ("fsd_format_duration");
        printf ("%s\n", buf);
    }
    else {
        unsigned long int sec;
        /*  Report whole seconds remaining in job, unless value is
         *  infinity, in which case we report UINT_MAX, or if value
         *  is 0 < t < 1, in which case round up to 1 to avoid
         *  printing "0" which would mean the job has expired.
         */
        if (isinf (t))
            sec = UINT_MAX;
        else if ((sec = floor (t)) == 0 && t > 0.)
            sec = 1;
        printf ("%lu\n", sec);
    }
    flux_close (h);
    return 0;
}



/* vi: ts=4 sw=4 expandtab
 */
