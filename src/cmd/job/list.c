/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job list, list-inactive, list-ids */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/optparse.h>


#include "src/common/libutil/log.h"

#include "common.h"

struct optparse_option list_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "states", .key = 's', .has_arg = 1, .arginfo = "STATES",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "List jobs in specific states",
    },
    { .name = "user", .key = 'u', .has_arg = 1, .arginfo = "USER",
      .usage = "Limit output to specific user. " \
               "Specify \"all\" for all users.",
    },
    { .name = "all-user", .key = 'a', .has_arg = 0,
      .usage = "List my jobs, regardless of state",
    },
    { .name = "all", .key = 'A', .has_arg = 0,
      .usage = "List jobs for all users, regardless of state",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option list_inactive_opts[] =  {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N",
      .usage = "Limit output to N jobs",
    },
    { .name = "since", .key = 's', .has_arg = 1, .arginfo = "T",
      .usage = "Limit output to jobs that entered the inactive state since"
               " timestamp T",
    },
    OPTPARSE_TABLE_END
};

struct optparse_option list_ids_opts[] =  {
    { .name = "wait-state", .key = 'W', .has_arg = 1, .arginfo = "STATE",
      .usage = "Return only after jobid has reached specified state",
    },
    OPTPARSE_TABLE_END
};

int cmd_list (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    flux_t *h;
    flux_future_t *f;
    uint32_t userid;
    int states = 0;
    json_t *c;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (optparse_hasopt (p, "all-user") || optparse_hasopt (p, "all"))
        states = FLUX_JOB_STATE_ACTIVE | FLUX_JOB_STATE_INACTIVE;
    else if (optparse_hasopt (p, "states"))
        states = parse_arg_states (p, "states");
    else
        states = FLUX_JOB_STATE_PENDING | FLUX_JOB_STATE_RUNNING;

    if (optparse_hasopt (p, "all"))
        userid = FLUX_USERID_UNKNOWN;
    else if (optparse_hasopt (p, "user"))
        userid = parse_arg_userid (p, "user");
    else
        userid = getuid ();

    if (!(c = json_pack ("{ s:[ {s:[i]}, {s:[i]} ] }",
                         "and",
                         "userid", userid,
                         "states", states)))
        log_msg_exit ("failed to construct constraint object");

    if (!(f = flux_rpc_pack (h,
                             "job-list.list",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:i s:[s] s:o}",
                             "max_entries", max_entries,
                             "attrs", "all",
                             "constraint", c)))
        log_err_exit ("flux_rpc_pack");
    while (1) {
        json_t *jobs;
        size_t index;
        json_t *value;
        if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("flux job-list.list: %s", future_strerror (f, errno));
        }
        json_array_foreach (jobs, index, value) {
            char *str = json_dumps (value, 0);
            if (!str)
                log_msg_exit ("error parsing list response");
            printf ("%s\n", str);
            free (str);
        }
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    flux_close (h);

    return (0);
}

int cmd_list_inactive (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    int max_entries = optparse_get_int (p, "count", 0);
    double since = optparse_get_double (p, "since", 0.);
    flux_t *h;
    flux_future_t *f;
    json_t *c;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if (optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(c = json_pack ("{s:[i]}", "states", FLUX_JOB_STATE_INACTIVE)))
        log_msg_exit ("failed to construct constraint object");

    if (!(f = flux_rpc_pack (h,
                             "job-list.list",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:i s:f s:[s] s:o}",
                             "max_entries", max_entries,
                             "since", since,
                             "attrs", "all",
                             "constraint", c)))
        log_err_exit ("flux_rpc_pack");
    while (1) {
        json_t *jobs;
        size_t index;
        json_t *value;
        if (flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
            if (errno == ENODATA)
                break;
            log_msg_exit ("flux job-list.list: %s", future_strerror (f, errno));
        }
        json_array_foreach (jobs, index, value) {
            char *str = json_dumps (value, 0);
            if (!str)
                log_msg_exit ("error parsing list response");
            printf ("%s\n", str);
            free (str);
        }
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    flux_close (h);

    return (0);
}

void list_id_continuation (flux_future_t *f, void *arg)
{
    json_t *job;
    char *str;
    if (flux_rpc_get_unpack (f, "{s:o}", "job", &job) < 0)
        log_msg_exit ("flux job-list.list-d: %s", future_strerror (f, errno));
    str = json_dumps (job, 0);
    if (!str)
        log_msg_exit ("error parsing list-id response");
    printf ("%s\n", str);
    free (str);
    flux_future_destroy (f);
}

int cmd_list_ids (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    int i, ids_len;
    flux_job_state_t state;
    const char *state_str;

    if (isatty (STDOUT_FILENO)) {
        fprintf (stderr,
                 "This is not the command you are looking for. "
                 "Try flux-jobs(1).\n");
        exit (1);
    }
    if ((argc - optindex) < 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* if no job state specified by user, pick first job state of
     * depend, which means will return as soon as the job-list module
     * is aware of the job
     */
    state_str = optparse_get_str (p, "wait-state", "depend");
    if (flux_job_strtostate (state_str, &state) < 0)
        log_msg_exit ("invalid job state specified");

    ids_len = argc - optindex;
    for (i = 0; i < ids_len; i++) {
        flux_jobid_t id = parse_jobid (argv[optindex + i]);
        flux_future_t *f;
        if (!(f = flux_rpc_pack (h,
                                 "job-list.list-id",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "attrs",
                                   "all",
                                 "state", state)))
            log_err_exit ("flux_rpc_pack");
        if (flux_future_then (f, -1, list_id_continuation, NULL) < 0)
            log_err_exit ("flux_future_then");
    }

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);

    return (0);
}

/*  vi: ts=4 sw=4 expandtab
 */
