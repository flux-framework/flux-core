/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_data.c - primary struct job helper functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librlist/rlist.h"
#include "src/common/librlist/rnode.h"
#include "src/common/libccan/ccan/str/str.h"
#include "src/common/libjob/jj.h"

#include "job_data.h"

void job_destroy (void *data)
{
    struct job *job = data;
    if (job) {
        free (job->ranks);
        free (job->nodelist);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        json_decref (job->jobspec);
        json_decref (job->R);
        json_decref (job->exception_context);
        zlist_destroy (&job->next_states);
        free (job);
    }
}

struct job *job_create (struct list_ctx *ctx, flux_jobid_t id)
{
    struct job *job = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->h = ctx->h;
    job->ctx = ctx;
    job->id = id;
    job->userid = FLUX_USERID_UNKNOWN;
    job->urgency = -1;
    /* pending jobs that are not yet assigned a priority shall be
     * listed after those who do, so we set the job priority to MIN */
    job->priority = FLUX_JOB_PRIORITY_MIN;
    job->state = FLUX_JOB_STATE_NEW;
    job->ntasks = -1;
    job->duration = -1.0;
    job->nnodes = -1;
    job->expiration = -1.0;
    job->wait_status = -1;
    job->result = FLUX_JOB_RESULT_FAILED;

    if (!(job->next_states = zlist_new ())) {
        errno = ENOMEM;
        job_destroy (job);
        return NULL;
    }

    job->states_mask = FLUX_JOB_STATE_NEW;
    job->states_events_mask = FLUX_JOB_STATE_NEW;
    job->eventlog_seq = -1;
    return job;
}

/* Return basename of path if there is a '/' in path.  Otherwise return
 * full path */
static const char *parse_job_name (const char *path)
{
    char *p = strrchr (path, '/');
    if (p) {
        p++;
        /* user mistake, specified a directory with trailing '/',
         * return full path */
        if (*p == '\0')
            return path;
        return p;
    }
    return path;
}

static int parse_jobspec_job_name (struct job *job,
                                   json_t *jobspec_job,
                                   json_t *tasks)
{
    json_error_t error;

    if (jobspec_job) {
        if (json_unpack_ex (jobspec_job, &error, 0,
                            "{s?:s}",
                            "name", &job->name) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid job dictionary: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return -1;
        }
    }

    /* If user did not specify job.name, we treat arg 0 of the command
     * as the job name */
    if (!job->name) {
        json_t *command = NULL;
        json_t *arg0;

        if (json_unpack_ex (tasks, &error, 0,
                            "[{s:o}]",
                            "command", &command) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return -1;
        }

        if (!json_is_array (command)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid jobspec",
                      __FUNCTION__, (uintmax_t)job->id);
            return -1;
        }

        arg0 = json_array_get (command, 0);
        if (!arg0 || !json_is_string (arg0)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid job command",
                      __FUNCTION__, (uintmax_t)job->id);
            return -1;
        }
        job->name = parse_job_name (json_string_value (arg0));
        assert (job->name);
    }

    return 0;
}

static int parse_jobspec_nnodes (struct job *job, struct jj_counts *jj)
{
    /* Set job->nnodes if it is available, otherwise it will be set
     * later when R is available.
     */
    if (jj->nnodes > 0)
        job->nnodes = jj->nnodes;

    return 0;
}

static int parse_jobspec_duration (struct job *job, struct jj_counts *jj)
{
    /* N.B. Jobspec V1 requires duration to be set, so duration will
     * always be >= 0 from libjj.
     */
    job->duration = jj->duration;
    return 0;
}

static int parse_per_resource (struct job *job,
                               const char **type,
                               int *count)
{
    json_error_t error;
    json_t *o = NULL;

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s:{s?:{s?:{s?:o}}}}}",
                        "attributes",
                          "system",
                            "shell",
                              "options",
                                "per-resource", &o) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        return -1;
    }

    (*count) = 1;
    if (o) {
        if (json_unpack_ex (o, &error, 0,
                            "{s:s s?:i}",
                            "type", type,
                            "count", count) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid per-resource spec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            return -1;
        }
    }

    return 0;
}

static int parse_jobspec_ntasks (struct job *job, struct jj_counts *jj)
{
    const char *type = NULL;
    int count = 0;

    /* per-resource is used to overcome short-term gaps in
     * Jobspec V1.  Remove per-resource logic below when it
     * has been retired
     */

    if (parse_per_resource (job, &type, &count) < 0)
        return -1;

    if (type && count > 0) {
        /* if per-resource type == nodes and nodes specified
         * (node->slot->core), this is a special case of ntasks.
         */
        if (streq (type, "node") && jj->nnodes > 0) {
            job->ntasks = jj->nnodes * count;
            return 0;
        }
        if (streq (type, "core")) {
            if (jj->nnodes == 0)
                job->ntasks = jj->nslots * jj->slot_size * count;
            else {
                /* if nnodes > 0, can't determine until nodes
                 * allocated and number of cores on node(s) are known.
                 * Set a flag / count to retrieve data later when
                 * R has been retrieved.
                 */
                job->ntasks_per_core_on_node_count = count;
            }
            return 0;
        }
    }

    job->ntasks = jj->nslots;
    return 0;
}

int job_parse_jobspec (struct job *job, const char *s)
{
    struct jj_counts jj;
    json_error_t error;
    json_t *jobspec_job = NULL;
    json_t *tasks;
    int rc = -1;

    if (!(job->jobspec = json_loads (s, 0, &error))) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto error;
    }

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s:{s?:o}}}",
                        "attributes",
                        "system",
                        "job",
                        &jobspec_job) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (jobspec_job) {
        if (!json_is_object (jobspec_job)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid jobspec",
                      __FUNCTION__, (uintmax_t)job->id);
            goto nonfatal_error;
        }
    }

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:o}",
                        "tasks", &tasks) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (parse_jobspec_job_name (job, jobspec_job, tasks) < 0)
        goto nonfatal_error;

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s:{s?:s}}}",
                        "attributes",
                        "system",
                        "queue", &job->queue) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (jj_get_counts_json (job->jobspec, &jj) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec; %s",
                  __FUNCTION__, (uintmax_t)job->id, jj.error);
        goto nonfatal_error;
    }

    if (parse_jobspec_nnodes (job, &jj) < 0)
        goto nonfatal_error;

    if (parse_jobspec_ntasks (job, &jj) < 0)
        goto nonfatal_error;

    if (parse_jobspec_duration (job, &jj) < 0)
        goto nonfatal_error;

    /* nonfatal error - jobspec illegal, but we'll continue on.  job
     * listing will return whatever data is available */
nonfatal_error:
    rc = 0;
error:
    return rc;
}

int job_parse_R (struct job *job, const char *s)
{
    struct rlist *rl = NULL;
    struct idset *idset = NULL;
    struct hostlist *hl = NULL;
    json_error_t error;
    int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;
    int saved_errno, rc = -1;

    if (!(job->R = json_loads (s, 0, &error))) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid R: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (!(rl = rlist_from_json (job->R, &error))) {
        flux_log_error (job->h, "rlist_from_json: %s", error.text);
        goto nonfatal_error;
    }

    job->expiration = rl->expiration;

    if (!(idset = rlist_ranks (rl)))
        goto nonfatal_error;

    job->nnodes = idset_count (idset);
    if (!(job->ranks = idset_encode (idset, flags)))
        goto nonfatal_error;

    /* reading nodelist from R directly would avoid the creation /
     * destruction of a hostlist.  However, we get a hostlist to
     * ensure that the nodelist we return to users is consistently
     * formatted.
     */
    if (!(hl = rlist_nodelist (rl)))
        goto nonfatal_error;

    if (!(job->nodelist = hostlist_encode (hl)))
        goto nonfatal_error;

    if (job->ntasks_per_core_on_node_count > 0) {
        int core_count = 0;
        struct rnode *rnode = zlistx_first (rl->nodes);
        while (rnode) {
            core_count += idset_count (rnode->cores->ids);
            rnode = zlistx_next (rl->nodes);
        }
        job->ntasks = core_count * job->ntasks_per_core_on_node_count;
    }

    /* nonfatal error - invalid R, but we'll continue on.  job listing
     * will get initialized data */
nonfatal_error:
    rc = 0;
    saved_errno = errno;
    hostlist_destroy (hl);
    idset_destroy (idset);
    rlist_destroy (rl);
    errno = saved_errno;
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
