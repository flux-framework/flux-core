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

struct res_level {
    const char *type;
    int count;
    json_t *with;
};

static int parse_res_level (struct job *job,
                            json_t *o,
                            struct res_level *resp)
{
    json_error_t error;
    struct res_level res;

    res.with = NULL;
    /* For jobspec version 1, expect exactly one array element per level.
     */
    if (json_unpack_ex (o, &error, 0,
                        "[{s:s s:i s?o}]",
                        "type", &res.type,
                        "count", &res.count,
                        "with", &res.with) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        return -1;
    }
    *resp = res;
    return 0;
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

int job_parse_jobspec (struct job *job, const char *s)
{
    json_error_t error;
    json_t *jobspec_job = NULL;
    json_t *command = NULL;
    json_t *tasks, *resources;
    struct res_level res[3];
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
    if (json_unpack_ex (tasks, &error, 0,
                        "[{s:o}]",
                        "command", &command) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (!json_is_array (command)) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec",
                  __FUNCTION__, (uintmax_t)job->id);
        goto nonfatal_error;
    }

    if (jobspec_job) {
        if (json_unpack_ex (jobspec_job, &error, 0,
                            "{s?:s}",
                            "name", &job->name) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid job dictionary: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            goto nonfatal_error;
        }
    }

    /* If user did not specify job.name, we treat arg 0 of the command
     * as the job name */
    if (!job->name) {
        json_t *arg0 = json_array_get (command, 0);
        if (!arg0 || !json_is_string (arg0)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid job command",
                      __FUNCTION__, (uintmax_t)job->id);
            goto nonfatal_error;
        }
        job->name = parse_job_name (json_string_value (arg0));
        assert (job->name);
    }

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:o}",
                        "resources", &resources) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    /* For jobspec version 1, expect either:
     * - node->slot->core->NIL
     * - slot->core->NIL
     */
    memset (res, 0, sizeof (res));
    if (parse_res_level (job, resources, &res[0]) < 0)
        goto nonfatal_error;
    if (res[0].with && parse_res_level (job, res[0].with, &res[1]) < 0)
        goto nonfatal_error;
    if (res[1].with && parse_res_level (job, res[1].with, &res[2]) < 0)
        goto nonfatal_error;

    /* Set job->nnodes if available.  In jobspec version 1, only if
     * resources listed as node->slot->core->NIL
     */
    if (res[0].type != NULL && !strcmp (res[0].type, "node")
        && res[1].type != NULL && !strcmp (res[1].type, "slot")
        && res[2].type != NULL && !strcmp (res[2].type, "core")
        && res[2].with == NULL)
        job->nnodes = res[0].count;

    /* Set job->ntasks
     */
    if (json_unpack_ex (tasks, NULL, 0,
                        "[{s:{s:i}}]",
                        "count", "total", &job->ntasks) < 0) {
        int per_slot, slot_count = 0;

        if (json_unpack_ex (tasks, &error, 0,
                            "[{s:{s:i}}]",
                            "count", "per_slot", &per_slot) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            goto nonfatal_error;
        }
        if (per_slot != 1) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %ju: per_slot count: expected 1 got %d",
                      __FUNCTION__, (uintmax_t)job->id, per_slot);
            goto nonfatal_error;
        }
        if (res[0].type != NULL && !strcmp (res[0].type, "slot")
            && res[1].type != NULL && !strcmp (res[1].type, "core")
            && res[1].with == NULL) {
            slot_count = res[0].count;
        }
        else if (res[0].type != NULL && !strcmp (res[0].type, "node")
                 && res[1].type != NULL && !strcmp (res[1].type, "slot")
                 && res[2].type != NULL && !strcmp (res[2].type, "core")
                 && res[2].with == NULL) {
            slot_count = res[0].count * res[1].count;
        }
        else {
            flux_log (job->h, LOG_WARNING,
                      "%s: job %ju: Unexpected resources: %s->%s->%s%s",
                      __FUNCTION__,
                      (uintmax_t)job->id,
                      res[0].type ? res[0].type : "NULL",
                      res[1].type ? res[1].type : "NULL",
                      res[2].type ? res[2].type : "NULL",
                      res[2].with ? "->..." : NULL);
            slot_count = -1;
        }
        job->ntasks = slot_count;
    }

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
