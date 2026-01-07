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
#include "src/common/libjob/idf58.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"

#include "job_data.h"

void job_destroy (void *data)
{
    struct job *job = data;
    if (job) {
        int save_errno = errno;
        free (job->ranks);
        free (job->nodelist);
        hostlist_destroy (job->nodelist_hl);
        idset_destroy (job->ranks_idset);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        json_decref (job->jobspec);
        json_decref (job->R);
        json_decref (job->exception_context);
        free (job);
        errno = save_errno;
    }
}

struct job *job_create (flux_t *h, flux_jobid_t id)
{
    struct job *job = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->h = h;
    job->id = id;
    job->userid = FLUX_USERID_UNKNOWN;
    job->urgency = -1;
    /* pending jobs that are not yet assigned a priority shall be
     * listed after those who do, so we set the job priority to MIN */
    job->priority = FLUX_JOB_PRIORITY_MIN;
    job->state = FLUX_JOB_STATE_NEW;
    job->ntasks = -1;
    job->ncores = -1;
    job->duration = -1.0;
    job->nnodes = -1;
    job->expiration = -1.0;
    job->wait_status = -1;
    job->result = FLUX_JOB_RESULT_FAILED;
    job->states_mask = FLUX_JOB_STATE_NEW;
    job->states_events_mask = FLUX_JOB_STATE_NEW;
    return job;
}

/* Return basename of path if there is a '/' in path.  Otherwise return
 * full path */
static const char *parse_job_name (const char *path)
{
    const char *p = strrchr (path, '/');
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
                                   json_t *jobspec_job)
{
    json_error_t error;

    if (jobspec_job) {
        if (json_unpack_ex (jobspec_job, &error, 0,
                            "{s?s}",
                            "name", &job->name) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid job dictionary: %s",
                      __FUNCTION__, idf58 (job->id), error.text);
            return -1;
        }
    }
    else
        job->name = NULL;

    /* If user did not specify job.name, we treat arg 0 of the command
     * as the job name */
    if (!job->name) {
        json_t *command = NULL;
        json_t *arg0;
        json_t *tasks;

        if (json_unpack_ex (job->jobspec, &error, 0,
                            "{s:o}",
                            "tasks", &tasks) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid jobspec: %s",
                      __FUNCTION__, idf58 (job->id), error.text);
            return -1;
        }

        if (json_unpack_ex (tasks, &error, 0,
                            "[{s:o}]",
                            "command", &command) < 0) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid jobspec: %s",
                      __FUNCTION__, idf58 (job->id), error.text);
            return -1;
        }

        if (!json_is_array (command)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid jobspec",
                      __FUNCTION__, idf58 (job->id));
            return -1;
        }

        arg0 = json_array_get (command, 0);
        if (!arg0 || !json_is_string (arg0)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid job command",
                      __FUNCTION__, idf58 (job->id));
            return -1;
        }
        job->name = parse_job_name (json_string_value (arg0));
        assert (job->name);
    }

    return 0;
}

static int parse_attributes_dict (struct job *job)
{
    json_error_t error;
    json_t *jobspec_job = NULL;

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s?{s?o}}}",
                        "attributes",
                        "system",
                        "job",
                        &jobspec_job) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid jobspec: %s",
                  __FUNCTION__, idf58 (job->id), error.text);
        return -1;
    }

    if (jobspec_job) {
        if (!json_is_object (jobspec_job)) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid jobspec",
                      __FUNCTION__, idf58 (job->id));
            return -1;
        }
    }

    if (parse_jobspec_job_name (job, jobspec_job) < 0)
        return -1;

    /* N.B. attributes.system.duration is required in jobspec version 1 */
    /* N.B. cwd & queue are optional, reset to NULL before parse in case
     * not listed
     */
    job->cwd = NULL;
    job->queue = NULL;
    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s?{s?s s?s s:F s?s s?s}}}",
                        "attributes",
                        "system",
                        "cwd", &job->cwd,
                        "queue", &job->queue,
                        "duration", &job->duration,
                        "project", &job->project,
                        "bank", &job->bank) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid jobspec: %s",
                  __FUNCTION__, idf58 (job->id), error.text);
        return -1;
    }

    return 0;
}

static int parse_jobspec_nnodes (struct job *job, struct jj_counts *jj)
{
    /* Set job->nnodes if it is available, otherwise it will be set
     * later when R is available.
     */
    if (jj->nnodes)
        job->nnodes = jj->nnodes;
    else
        job->nnodes = -1;

    return 0;
}

static int parse_per_resource (struct job *job,
                               const char **type,
                               int *count)
{
    json_error_t error;
    json_t *o = NULL;

    if (json_unpack_ex (job->jobspec, &error, 0,
                        "{s:{s?{s?{s?{s?o}}}}}",
                        "attributes",
                          "system",
                            "shell",
                              "options",
                                "per-resource", &o) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid jobspec: %s",
                  __FUNCTION__, idf58 (job->id), error.text);
        return -1;
    }

    (*count) = 1;
    if (o) {
        if (json_unpack_ex (o, &error, 0,
                            "{s:s s?i}",
                            "type", type,
                            "count", count) < 0 || (*count) < 1) {
            flux_log (job->h, LOG_ERR,
                      "%s: job %s invalid per-resource spec: %s",
                      __FUNCTION__, idf58 (job->id), error.text);
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

    if (type) {
        /* if per-resource type == nodes and nodes specified
         * (node->slot->core), this is a special case of ntasks.
         */
        if (streq (type, "node")) {
            if (jj->nnodes) {
                job->ntasks = jj->nnodes * count;
            } else {
                /* if nnodes == 0, can't determine until nodes allocated.
                 * Set a flag / count to retrieve data later when
                 * R has been retrieved.
                 */
                job->ntasks_per_node_on_node_count = count;
                job->ntasks = -1;
            }
            return 0;
        } else if (streq (type, "core")) {
            if (!jj->nnodes)
                job->ntasks = jj->nslots * jj->slot_size * count;
            else {
                /* if nnodes > 0, can't determine until nodes
                 * allocated and number of cores on node(s) are known.
                 * Set a flag / count to retrieve data later when
                 * R has been retrieved.
                 */
                job->ntasks_per_core_on_node_count = count;
                job->ntasks = -1;
            }
            return 0;
        }
    }

    if (json_unpack_ex (job->jobspec, NULL, 0,
                        "{s:[{s:{s:i}}]}",
                        "tasks",
                        "count",
                        "total", &job->ntasks) < 0)
        job->ntasks = jj->nslots;
    return 0;
}

static int parse_jobspec_ncores (struct job *job, struct jj_counts *jj)
{
    /* number of cores can't be determined yet, calculate later when R
     * is parsed */
    if (jj->nnodes && jj->exclusive) {
        job->ncores = -1;
        return 0;
    }

    /* nslots already accounts for nnodes if available */
    job->ncores = jj->nslots * jj->slot_size;
    return 0;
}

static int load_jobspec (struct job *job, const char *s, bool allow_nonfatal)
{
    json_error_t error;

    if (!(job->jobspec = json_loads (s, 0, &error))) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid jobspec: %s",
                  __FUNCTION__, idf58 (job->id), error.text);
        return allow_nonfatal ? 0 : -1;
    }
    return 0;
}

static int parse_jobspec (struct job *job, bool allow_nonfatal)
{
    struct jj_counts jj;

    if (parse_attributes_dict (job) < 0)
        goto nonfatal_error;

    if (jj_get_counts_json (job->jobspec, &jj) < 0) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid jobspec; %s",
                  __FUNCTION__, idf58 (job->id), jj.error);
        goto nonfatal_error;
    }

    if (parse_jobspec_nnodes (job, &jj) < 0)
        goto nonfatal_error;

    if (parse_jobspec_ntasks (job, &jj) < 0)
        goto nonfatal_error;

    if (parse_jobspec_ncores (job, &jj) < 0)
        goto nonfatal_error;

    return 0;

    /* nonfatal error - jobspec illegal, but we'll continue on.  job
     * listing will return whatever data is available */
nonfatal_error:
    return allow_nonfatal ? 0 : -1;
}

int job_parse_jobspec_cached (struct job *job, json_t *updates)
{
    if (!job->jobspec) {
        errno = EINVAL;
        return -1;
    }
    if (parse_jobspec (job, true) < 0)
        return -1;
    return job_jobspec_update (job, updates);
}

int job_parse_jobspec (struct job *job, const char *s, json_t *updates)
{
    if (load_jobspec (job, s, true) < 0)
        return -1;
    return job_parse_jobspec_cached (job, updates);
}

int job_parse_jobspec_fatal (struct job *job, const char *s, json_t *updates)
{
    if (load_jobspec (job, s, false) < 0)
        return -1;
    if (parse_jobspec (job, false) < 0)
        return -1;
    return job_jobspec_update (job, updates);
}

static int load_R (struct job *job, const char *s, bool allow_nonfatal)
{
    json_error_t error;

    if (!(job->R = json_loads (s, 0, &error))) {
        flux_log (job->h, LOG_ERR,
                  "%s: job %s invalid R: %s",
                  __FUNCTION__, idf58 (job->id), error.text);
        return allow_nonfatal ? 0 : -1;
    }
    return 0;
}

static int parse_R (struct job *job, bool allow_nonfatal)
{
    struct rlist *rl = NULL;
    struct idset *idset = NULL;
    struct hostlist *hl = NULL;
    json_error_t error;
    int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;
    int core_count = 0;
    struct rnode *rnode;
    int saved_errno, rc = -1;
    char *tmp;

    if (!(rl = rlist_from_json (job->R, &error))) {
        flux_log_error (job->h, "rlist_from_json: %s", error.text);
        goto nonfatal_error;
    }

    job->expiration = rl->expiration;

    if (!(idset = rlist_ranks (rl)))
        goto nonfatal_error;

    job->nnodes = idset_count (idset);
    if (job->ntasks_per_node_on_node_count)
        job->ntasks = job->nnodes * job->ntasks_per_node_on_node_count;
    if (!(tmp = idset_encode (idset, flags)))
        goto nonfatal_error;
    free (job->ranks);
    job->ranks = tmp;

    /* reading nodelist from R directly would avoid the creation /
     * destruction of a hostlist.  However, we get a hostlist to
     * ensure that the nodelist we return to users is consistently
     * formatted.
     */
    if (!(hl = rlist_nodelist (rl)))
        goto nonfatal_error;

    if (!(tmp = hostlist_encode (hl)))
        goto nonfatal_error;
    free (job->nodelist);
    job->nodelist = tmp;

    rnode = zlistx_first (rl->nodes);
    while (rnode) {
        core_count += idset_count (rnode->cores->ids);
        rnode = zlistx_next (rl->nodes);
    }
    job->ncores = core_count;

    if (job->ntasks_per_core_on_node_count)
        job->ntasks = core_count * job->ntasks_per_core_on_node_count;

    rc = 0;
    goto cleanup;

    /* nonfatal error - invalid R, but we'll continue on.  job listing
     * will get initialized data */
nonfatal_error:
    rc = allow_nonfatal ? 0 : -1;
cleanup:
    saved_errno = errno;
    hostlist_destroy (hl);
    idset_destroy (idset);
    rlist_destroy (rl);
    errno = saved_errno;
    return rc;
}

int job_parse_R_cached (struct job *job, json_t *updates)
{
    if (!job->R) {
        errno = EINVAL;
        return -1;
    }
    if (parse_R (job, true) < 0)
        return -1;
    return job_R_update (job, updates);
}

int job_parse_R (struct job *job, const char *s, json_t *updates)
{
    if (load_R (job, s, true) < 0)
        return -1;
    return job_parse_R_cached (job, updates);
}

int job_parse_R_fatal (struct job *job, const char *s, json_t *updates)
{
    if (load_R (job, s, false) < 0)
        return -1;
    if (parse_R (job, false) < 0)
        return -1;
    return job_R_update (job, updates);
}

int job_jobspec_update (struct job *job, json_t *updates)
{
    const char *key;
    json_t *value;

    if (!updates)
        return 0;

    /* To be on safe side, we should probably copy job->jobspec and
     * only apply updates if they succeed and are parsed.  However, we
     * don't do that given low odds of invalid updates ever happening.
     */
    json_object_foreach (updates, key, value) {
        /* In jobspec V1 only valid keys in a jobspec are resources,
         * tasks, and attributes
         */
        if ((!streq (key, "resources")
             && !strstarts (key, "resources.")
             && !streq (key, "tasks")
             && !strstarts (key, "tasks.")
             && !streq (key, "attributes")
             && !strstarts (key, "attributes."))
            || jpath_set (job->jobspec, key, value) < 0)
            flux_log (job->h, LOG_INFO,
                      "%s: job %s failed to update jobspec key %s",
                      __FUNCTION__, idf58 (job->id), key);
    }
    return parse_jobspec (job, false);
}

int job_R_update (struct job *job, json_t *updates)
{
    const char *key;
    json_t *value;

    if (!updates)
        return 0;

    json_object_foreach (updates, key, value) {
        /* RFC 21 resource-update event only allows update
         * to:
         * - expiration
         */
        if (streq (key, "expiration"))
            if (jpath_set (job->R, "execution.expiration", value) < 0)
                flux_log (job->h, LOG_INFO,
                          "%s: job %s failed to update R key %s",
                          __FUNCTION__, idf58 (job->id), key);
    }

    return parse_R (job, false);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
