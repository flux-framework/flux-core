/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job wait */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libjob/idf58.h"
#include "common.h"

struct optparse_option wait_opts[] =  {
    { .name = "all", .key = 'a', .has_arg = 0,
      .usage = "Wait for all (waitable) jobs",
    },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Emit a line of output for all jobs, not just failing ones",
    },
    OPTPARSE_TABLE_END
};

int cmd_wait (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_jobid_t id = FLUX_JOBID_ANY;
    bool success;
    const char *errstr;
    int rc = 0;

    if ((argc - optindex) > 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < argc) {
        id = parse_jobid (argv[optindex++]);
        if (optparse_hasopt (p, "all"))
            log_err_exit ("jobid not supported with --all");
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "all")) {
        for (;;) {
            if (!(f = flux_job_wait (h, FLUX_JOBID_ANY)))
                log_err_exit ("flux_job_wait");
            if (flux_job_wait_get_status (f, &success, &errstr) < 0) {
                if (errno == ECHILD) { // no more waitable jobs
                    flux_future_destroy (f);
                    break;
                }
                log_msg_exit ("flux_job_wait_get_status: %s",
                              future_strerror (f, errno));
            }
            if (flux_job_wait_get_id (f, &id) < 0)
                log_msg_exit ("flux_job_wait_get_id: %s",
                              future_strerror (f, errno));
            if (!success) {
                fprintf (stderr, "%s: %s\n", idf58 (id), errstr);
                rc = 1;
            }
            else {
                if (optparse_hasopt (p, "verbose"))
                    fprintf (stderr,
                             "%s: job completed successfully\n",
                             idf58 (id));
            }
            flux_future_destroy (f);
        }
    }
    else {
        if (!(f = flux_job_wait (h, id)))
            log_err_exit ("flux_job_wait");
        if (flux_job_wait_get_status (f, &success, &errstr) < 0) {
            /* ECHILD == no more waitable jobs or not waitable,
             * exit code 2 instead of 1 */
            if (errno == ECHILD)
                rc = 2;
            else
                rc = 1;
            log_msg ("%s", flux_future_error_string (f));
            goto out;
        }
        if (id == FLUX_JOBID_ANY) {
            if (flux_job_wait_get_id (f, &id) < 0)
                log_err_exit ("flux_job_wait_get_id");
            printf ("%s\n", idf58 (id));
        }
        if (!success)
            log_msg_exit ("%s", errstr);
        flux_future_destroy (f);
    }
out:
    flux_close (h);
    return (rc);
}




/* vi: ts=4 sw=4 expandtab
 */
