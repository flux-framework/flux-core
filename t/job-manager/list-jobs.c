/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <czmq.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <jansson.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
#include "src/common/libutil/log.h"

static struct optparse_option list_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    OPTPARSE_TABLE_END
};

/* convert floating point timestamp (UNIX epoch, UTC) to ISO 8601 string,
 * with second precision
 */
static int iso_timestr (double timestamp, char *buf, size_t size)
{
    time_t sec = timestamp;
    struct tm tm;

    if (!gmtime_r (&sec, &tm))
        return -1;
    if (strftime (buf, size, "%FT%TZ", &tm) == 0)
        return -1;
    return 0;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    optparse_t *opts;
    int max_entries;
    int optindex;
    flux_future_t *f;
    json_t *jobs;
    size_t index;
    json_t *value;

    log_init ("list-jobs");

    opts = optparse_create ("list-jobs");
    if (optparse_add_option_table (opts, list_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    max_entries = optparse_get_int (opts, "count", 0);

    if (optindex != argc) {
        optparse_print_usage (opts);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc_pack (h,
                             "job-manager.list",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i}",
                             "max_entries",
                             max_entries)))
        log_err_exit ("flux_rpc_pack");

    if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0)
        log_err_exit ("flux_rpc_get_unpack");

    json_array_foreach (jobs, index, value) {
        flux_jobid_t id;
        int urgency;
        uint32_t userid;
        double t_submit;
        char timestr[80];
        flux_job_state_t state;
        json_t *annotations = NULL;
        char *annotations_str = NULL;

        if (json_unpack (value, "{s:I s:i s:i s:f s:i s?:o}",
                                "id", &id,
                                "urgency", &urgency,
                                "userid", &userid,
                                "t_submit", &t_submit,
                                "state", &state,
                                "annotations", &annotations) < 0)
            log_msg_exit ("error parsing job data");
        if (iso_timestr (t_submit, timestr, sizeof (timestr)) < 0)
            log_err_exit ("time conversion error");
        if (annotations)
            annotations_str = json_dumps (annotations, 0);
        printf ("%ju\t%s\t%lu\t%d\t%s%s%s\n",
                (uintmax_t)id,
                flux_job_statetostr (state, true),
                (unsigned long)userid,
                urgency,
                timestr,
                annotations_str ? "\t" : "",
                annotations_str ? annotations_str : "");
        free (annotations_str);
    }

    flux_future_destroy (f);
    flux_close (h);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
