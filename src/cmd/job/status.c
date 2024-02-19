/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job status */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "common.h"

struct optparse_option status_opts[] = {
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Increase verbosity"
    },
    { .name = "exception-exit-code", .key = 'e', .has_arg = 1,
      .group = 1,
      .arginfo = "N",
      .usage = "Set the default exit code for any jobs that terminate"
               " solely due to an exception (e.g. canceled jobs or"
               " jobs rejected by the scheduler) to N [default=1]"
    },
    { .name = "json", .key = 'j', .has_arg = 0,
      .usage = "Dump job result information gleaned from eventlog to stdout",
    },
    OPTPARSE_TABLE_END
};

static void result_cb (flux_future_t *f, void *arg)
{}

/*  Translate status to an exit code as would be done by UNIX shell:
 */
static int status_to_exitcode (int status)
{
    if (status < 0)
        return 0;
    if (WIFSIGNALED (status))
        return 128 + WTERMSIG (status);
    return WEXITSTATUS (status);
}

int cmd_status (optparse_t *p, int argc, char **argv)
{
    flux_t *h = NULL;
    flux_future_t **futures = NULL;
    int exit_code;
    int i;
    int njobs;
    int verbose = optparse_getopt (p, "verbose", NULL);
    int json = optparse_getopt (p, "json", NULL);
    int optindex = optparse_option_index (p);
    int exception_exit_code = optparse_get_int (p, "exception-exit-code", 1);

    if ((njobs = (argc - optindex)) < 1) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(futures = calloc (njobs, sizeof (*futures))))
        log_err_exit ("Failed to initialize futures array");

    for (i = 0; i < njobs; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex+i]);
        if (!(futures[i] = flux_job_result (h, id, 0)))
            log_err_exit ("flux_job_wait_status");
        if (flux_future_then (futures[i], -1., result_cb, NULL) < 0)
            log_err_exit ("flux_future_then");
    }

    if (verbose && njobs > 1)
        log_msg ("fetching status for %d jobs", njobs);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err ("flux_reactor_run");

    if (verbose && njobs > 1)
        log_msg ("all done.");

    exit_code = 0;
    for (i = 0; i < njobs; i++) {
        const char *jobid = argv[optindex+i];
        int status = -1;
        int exitcode;
        int exception = 0;
        int exc_severity = 0;
        const char *exc_type = NULL;

        if (json) {
            const char *s = NULL;
            if (flux_job_result_get (futures[i], &s) < 0)
                log_err_exit ("flux_job_result_get");
            printf ("%s\n", s);
        }

        if (flux_job_result_get_unpack (futures[i],
                                        "{s:b s?s s?i s?i}",
                                        "exception_occurred", &exception,
                                        "exception_type", &exc_type,
                                        "exception_severity", &exc_severity,
                                        "waitstatus", &status) < 0) {
            if (errno == ENOENT)
                log_msg_exit ("%s: No such job", jobid);
            log_err_exit ("%s: flux_job_wait_status_get", jobid);
        }
        if ((exitcode = status_to_exitcode (status)) > exit_code)
            exit_code = exitcode;
        if (exception && exc_severity == 0 && exception_exit_code > exit_code)
            exit_code = exception_exit_code;
        if (optparse_hasopt (p, "verbose")) {
            if (WIFSIGNALED (status)) {
                log_msg ("%s: job shell died by signal %d",
                         jobid,
                         WTERMSIG (status));
            }
            else if (verbose > 1 || exitcode != 0) {
                if (exception && exc_severity == 0)
                    log_msg ("%s: exception type=%s",
                             jobid,
                             exc_type);
                else
                    log_msg ("%s: exited with exit code %d",
                             jobid,
                             exitcode);
            }
        }
        flux_future_destroy (futures[i]);
    }
    flux_close (h);
    free (futures);
    return exit_code;
}

/* vi: ts=4 sw=4 expandtab
 */
