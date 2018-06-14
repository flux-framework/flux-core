/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.  Additionally, the libflux-core library may be
 *  redistributed under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"
#include "src/common/libutil/fluid.h"
#include "src/common/liblsd/hash.h"

#include "jobdir.h"

struct job_manager_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct hash *active_jobs;   // hash by integer id
    zlistx_t *active_jobs_list; // list of hash entries, in numerical order
};

struct job {
    flux_jobid_t id;
    uint32_t userid;
    int priority;
};

/* hash_key_f for active_jobs hash
 */
static unsigned int job_hash (flux_jobid_t *key)
{
    return *key;
}

/* hash_cmp_f for active_jobs hash
 * (compares hash keys)
 */
static int job_hash_cmp (flux_jobid_t *key1, flux_jobid_t *key2)
{
    if (*key1 == *key2)
        return 0;
    return (*key1 < *key2 ? -1 : 1);
}


/* zlistx_comparator_fn for active_jobs_list
 * (compares elements)
 */
#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))
static int job_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

static void job_destroy (struct job *job)
{
    if (job) {
        int saved_errno = errno;
        free (job);
        errno = saved_errno;
    }
}

static struct job *job_create (flux_jobid_t id, int priority, uint32_t userid)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->id = id;
    job->userid = userid;
    job->priority = priority;
    return job;
}

/* Helper for submit_event_cb() and also callback for jobdir_map().
 * Inserts 'job' into active_jobs hash and active_jobs_list (pri,FLUID order).
 * N.B. zlistx_insert uses zlistx comparator function to insert in order,
 * and low_value=false indicates that search for position begins at end.
 */
int add_active_job (flux_jobid_t id, int priority, uint32_t userid, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    struct job *job;

    if (!(job = job_create (id, priority, userid)))
        return -1;
    if (!hash_insert (ctx->active_jobs, &job->id, job)) {
        job_destroy (job);
        return (errno == EEXIST ? 0 : -1); // EEXIST is not an error
    }
    if (!zlistx_insert (ctx->active_jobs_list, job, false)) {
        int saved_errno = errno;
        hash_remove (ctx->active_jobs, &job->id);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

/* job-ingest.submit event - add jobs to hash/list
 */
static void submit_event_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    json_t *jobs;
    int i;

    if (flux_event_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        return;
    }
    for (i = 0; i < json_array_size (jobs); i++) {
        json_t *el = json_array_get (jobs, i);
        flux_jobid_t id;
        uint32_t userid;
        int priority;

        if (!el || json_unpack (el, "{s:I s:i s:i}", "id", &id,
                                                     "priority", &priority,
                                                     "userid", &userid) < 0) {
            flux_log (h, LOG_ERR, "%s: error decoding job index %d",
                      __FUNCTION__, i);
            continue;
        }
        if (add_active_job (id, priority, userid, ctx) < 0) {
            flux_log_error (h, "%s: add job %llu",
                            __FUNCTION__, (unsigned long long)id);
            continue;
        }
    }
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__, i);
    return;
}

/* Create a job object for 'job'.
 * Add requested attributes in 'attrs', to be limited by requestor
 * creds (userid, rolemask).
 */
static json_t *list_job (struct job_manager_ctx *ctx, struct job *job,
                         json_t *attrs, uint32_t userid, uint32_t rolemask)
{
    size_t index;
    json_t *value;
    json_t *o;
    int saved_errno;

    if (!(o = json_object ()))
        goto error_nomem;
    json_array_foreach (attrs, index, value) {
        const char *attr = json_string_value (value);
        json_t *val = NULL;
        if (!attr) {
            errno = EPROTO;
            goto error;
        }
        if (!strcmp (attr, "id")) {
            val = json_integer (job->id);
        }
        else if (!strcmp (attr, "userid")) {
            val = json_integer (job->userid);
        }
        else if (!strcmp (attr, "priority")) {
            val = json_integer (job->priority);
        }
        else {
            errno = EPROTO;
            goto error;
        }
        if (val == NULL)
            goto error_nomem;
        if (json_object_set_new (o, attr, val) < 0) {
            json_decref (val);
            goto error_nomem;
        }
    }
    return o;
error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return NULL;
}

/* Create a JSON array of 'job' objects representing the
 * job manager's queue of jobs.  If max_entries > 0, then
 * return only the first max_entries jobs from the head of the queue.
 */
static json_t *list_jobs (struct job_manager_ctx *ctx,
                          uint32_t userid, uint32_t rolemask,
                          int max_entries, json_t *attrs)
{
    json_t *jobs;
    struct job *job;
    int saved_errno;

    if (!(jobs = json_array ()))
        goto error_nomem;
    job = zlistx_first (ctx->active_jobs_list);
    while (job) {
        json_t *o;
        if (!(o = list_job (ctx, job, attrs, userid, rolemask)))
            goto error;
        if (json_array_append_new (jobs, o) < 0) {
            json_decref (o);
            goto error_nomem;
        }
        if (json_array_size (jobs) == max_entries)
            break;
        job = zlistx_next (ctx->active_jobs_list);
    }
    return jobs;
error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (jobs);
    errno = saved_errno;
    return NULL;
}

static void list_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    int max_entries;
    json_t *jobs;
    json_t *attrs;
    uint32_t userid;
    uint32_t rolemask;

    if (flux_request_unpack (msg, NULL, "{s:i s:o}",
                                        "max_entries", &max_entries,
                                        "attrs", &attrs) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (max_entries < 0 || !json_is_array (attrs)) {
        errno = EPROTO;
        goto error;
    }
    if (!(jobs = list_jobs (ctx, userid, rolemask, max_entries, attrs)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:o}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (jobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static int walk_kvs_active (flux_t *h, struct job_manager_ctx *ctx)
{
    int count;

    if ((count = jobdir_map (h, "job.active", add_active_job, ctx)) < 0)
        return -1;
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__, count);
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,   "job-ingest.submit", submit_event_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.list", list_cb, FLUX_ROLE_USER},
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_manager_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    if (!(ctx.active_jobs = hash_create (0, (hash_key_f)job_hash,
                                        (hash_cmp_f)job_hash_cmp,
                                        (hash_del_f)job_destroy))) {
        flux_log_error (h, "list_create");
        goto done;
    }
    if (!(ctx.active_jobs_list = zlistx_new ())) {
        flux_log_error (h, "zlistx_new");
        goto done;
    }
    zlistx_set_comparator (ctx.active_jobs_list, job_cmp);
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_event_subscribe (h, "job-ingest.submit") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (walk_kvs_active (h, &ctx) < 0) {
        flux_log_error (h, "walk_kvs_active");
        goto done;
    }
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    if (ctx.active_jobs_list)
        zlistx_destroy (&ctx.active_jobs_list);
    if (ctx.active_jobs)
        hash_destroy (ctx.active_jobs);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
