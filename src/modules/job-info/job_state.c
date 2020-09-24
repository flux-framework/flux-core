/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_state.c - store information on state of jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libidset/idset.h"

#include "job_state.h"
#include "idsync.h"
#include "job_util.h"

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

struct state_transition {
    flux_job_state_t state;
    bool processed;
    double timestamp;
};

struct annotations_data {
    double timestamp;
    json_t *annotations;
};

static void process_next_state (struct info_ctx *ctx, struct job *job);

static void annotations_data_destroy_wrapper (void **data);

static void job_events_continuation (flux_future_t *f, void *arg);

/* Compare items for sorting in list, priority first (higher priority
 * before lower priority), t_submit second (earlier submission time
 * first) N.B. zlistx_comparator_fn signature
 */
static int job_priority_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->t_submit, j2->t_submit);
    return rc;
}

/* Compare items for sorting in list by timestamp (note that sorting
 * is in reverse order, most recently (i.e. bigger timestamp)
 * running/completed comes first).  N.B. zlistx_comparator_fn
 * signature
 */
static int job_running_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_run, j1->t_run);
}

static int job_inactive_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_inactive, j1->t_inactive);
}

static void job_destroy (void *data)
{
    struct job *job = data;
    if (job) {
        json_decref (job->exception_context);
        json_decref (job->annotations);
        json_decref (job->jobspec_job);
        json_decref (job->jobspec_cmd);
        json_decref (job->R);
        free (job->ranks);
        zlist_destroy (&job->next_states);
        free (job);
    }
}

static void job_destroy_wrapper (void **data)
{
    struct job **job = (struct job **)data;
    job_destroy (*job);
}

void *job_id_duplicator (const void *item)
{
    flux_jobid_t *idp = calloc (1, sizeof (flux_jobid_t));
    if (!idp)
        return NULL;
    *(idp) = *((flux_jobid_t *)item);
    return idp;
}

void job_id_destructor (void **item)
{
    flux_jobid_t *idp = *((flux_jobid_t **)item);
    free (idp);
}

void flux_msg_destroy_wrapper (void **data)
{
    if (data) {
        flux_msg_t **ptr = (flux_msg_t **)data;
        flux_msg_destroy (*ptr);
    }
}

static struct job *job_create (struct info_ctx *ctx, flux_jobid_t id)
{
    struct job *job = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->ctx = ctx;
    job->id = id;
    job->state = FLUX_JOB_NEW;
    job->userid = FLUX_USERID_UNKNOWN;
    job->priority = -1;

    if (!(job->next_states = zlist_new ())) {
        errno = ENOMEM;
        job_destroy (job);
        return NULL;
    }

    return job;
}

struct job_state_ctx *job_state_create (flux_t *h)
{
    struct job_state_ctx *jsctx = NULL;
    int saved_errno;

    if (!(jsctx = calloc (1, sizeof (*jsctx)))) {
        flux_log_error (h, "calloc");
        return NULL;
    }
    jsctx->h = h;

    /* Index is the primary data structure holding the job data
     * structures.  It is responsible for destruction.  Lists only
     * contain desired sort of jobs.
     */

    if (!(jsctx->index = job_hash_create ()))
        goto error;
    zhashx_set_destructor (jsctx->index, job_destroy_wrapper);

    if (!(jsctx->pending = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->pending, job_priority_cmp);

    if (!(jsctx->running = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->running, job_running_cmp);

    if (!(jsctx->inactive = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->inactive, job_inactive_cmp);

    if (!(jsctx->processing = zlistx_new ()))
        goto error;

    if (!(jsctx->futures = zlistx_new ()))
        goto error;

    if (!(jsctx->early_annotations = job_hash_create ())) {
        errno = ENOMEM;
        goto error;
    }
    /* job id not stored in job struct, have to duplicate it */
    zhashx_set_key_duplicator (jsctx->early_annotations, job_id_duplicator);
    zhashx_set_key_destructor (jsctx->early_annotations, job_id_destructor);
    zhashx_set_destructor (jsctx->early_annotations,
                           annotations_data_destroy_wrapper);

    if (!(jsctx->transitions = zlistx_new ()))
        goto error;
    zlistx_set_destructor (jsctx->transitions, flux_msg_destroy_wrapper);

    if (flux_event_subscribe (h, "job-state") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto error;
    }

    if (!(jsctx->events = flux_rpc_pack (jsctx->h,
                                         "job-manager.events",
                                         FLUX_NODEID_ANY,
                                         FLUX_RPC_STREAMING,
                                         "{s:{s:i s:i}}",
                                         "allow",
                                           "priority", 1,
                                           "annotations", 1))) {
        flux_log_error (jsctx->h, "flux_rpc_pack");
        goto error;
    }

    if (flux_future_then (jsctx->events,
                          -1,
                          job_events_continuation,
                          jsctx) < 0) {
        flux_log_error (jsctx->h, "flux_future_then");
        goto error;
    }

    return jsctx;

error:
    saved_errno = errno;
    job_state_destroy (jsctx);
    errno = saved_errno;
    return NULL;
}

void job_state_destroy (void *data)
{
    struct job_state_ctx *jsctx = data;
    if (jsctx) {
        /* Don't destroy processing until futures are complete */
        if (jsctx->futures) {
            flux_future_t *f;
            f = zlistx_first (jsctx->futures);
            while (f) {
                if (flux_future_get (f, NULL) < 0)
                    flux_log_error (jsctx->h, "%s: flux_future_get",
                                    __FUNCTION__);
                flux_future_destroy (f);
                f = zlistx_next (jsctx->futures);
            }
            zlistx_destroy (&jsctx->futures);
        }
        zhashx_destroy (&jsctx->early_annotations);
        /* Destroy index last, as it is the one that will actually
         * destroy the job objects */
        zlistx_destroy (&jsctx->processing);
        zlistx_destroy (&jsctx->inactive);
        zlistx_destroy (&jsctx->running);
        zlistx_destroy (&jsctx->pending);
        zhashx_destroy (&jsctx->index);
        zlistx_destroy (&jsctx->transitions);
        flux_future_destroy (jsctx->events);
        (void)flux_event_unsubscribe (jsctx->h, "job-state");
        free (jsctx);
    }
}

/* zlistx_insert() and zlistx_reorder() take a 'low_value' parameter
 * which indicates which end of the list to search from.
 * false=search begins at tail (lowest priority, youngest)
 * true=search begins at head (highest priority, oldest)
 * Attempt to minimize search distance based on job priority.
 */
static bool search_direction (struct job *job)
{
    if (job->priority > FLUX_JOB_PRIORITY_DEFAULT)
        return true;
    else
        return false;
}

static int *state_counter (struct info_ctx *ctx,
                           struct job *job,
                           flux_job_state_t state)
{
    if (state == FLUX_JOB_NEW)
        return NULL;
    else if (state == FLUX_JOB_DEPEND)
        return &ctx->jsctx->depend_count;
    else if (state == FLUX_JOB_SCHED)
        return &ctx->jsctx->sched_count;
    else if (state == FLUX_JOB_RUN)
        return &ctx->jsctx->run_count;
    else if (state == FLUX_JOB_CLEANUP)
        return &ctx->jsctx->cleanup_count;
    else if (state == FLUX_JOB_INACTIVE)
        return &ctx->jsctx->inactive_count;

    flux_log (ctx->h, LOG_ERR, "illegal state transition for job %ju: %d",
              (uintmax_t)job->id, state);
    return NULL;
}

static void update_job_state (struct info_ctx *ctx,
                              struct job *job,
                              flux_job_state_t new_state,
                              double timestamp)
{
    int *decrement;
    int *increment;

    decrement = state_counter (ctx, job, job->state);
    increment = state_counter (ctx, job, new_state);
    job->state = new_state;
    if (job->state == FLUX_JOB_DEPEND)
        job->t_submit = timestamp;
    else if (job->state == FLUX_JOB_SCHED)
        job->t_sched = timestamp;
    else if (job->state == FLUX_JOB_RUN)
        job->t_run = timestamp;
    else if (job->state == FLUX_JOB_CLEANUP)
        job->t_cleanup = timestamp;
    else if (job->state == FLUX_JOB_INACTIVE)
        job->t_inactive = timestamp;
    job->states_mask |= job->state;
    if (decrement)
        (*decrement)--;
    if (increment)
        (*increment)++;
}

static void job_insert_list (struct job_state_ctx *jsctx,
                             struct job *job,
                             flux_job_state_t newstate)
{
    /* Note: comparator is set for running & inactive lists, but the
     * sort calls are not called on zlistx_add_start() */
    if (newstate == FLUX_JOB_DEPEND
        || newstate == FLUX_JOB_SCHED) {
        if (!(job->list_handle = zlistx_insert (jsctx->pending,
                                                job,
                                                search_direction (job))))
            flux_log_error (jsctx->h, "%s: zlistx_insert",
                            __FUNCTION__);
    }
    else if (newstate == FLUX_JOB_RUN
             || newstate == FLUX_JOB_CLEANUP) {
        if (!(job->list_handle = zlistx_add_start (jsctx->running,
                                                   job)))
            flux_log_error (jsctx->h, "%s: zlistx_add_start",
                            __FUNCTION__);
    }
    else { /* newstate == FLUX_JOB_INACTIVE */
        if (!(job->list_handle = zlistx_add_start (jsctx->inactive,
                                                   job)))
            flux_log_error (jsctx->h, "%s: zlistx_add_start",
                            __FUNCTION__);
    }
}

/* remove job from one list and move it to another based on the
 * newstate */
static void job_change_list (struct job_state_ctx *jsctx,
                             struct job *job,
                             zlistx_t *oldlist,
                             flux_job_state_t newstate)
{
    if (zlistx_detach (oldlist, job->list_handle) < 0)
        flux_log_error (jsctx->h, "%s: zlistx_detach",
                        __FUNCTION__);
    job->list_handle = NULL;

    job_insert_list (jsctx, job, newstate);
}

static zlistx_t *get_list (struct job_state_ctx *jsctx, flux_job_state_t state)
{
    if (state == FLUX_JOB_NEW)
        return jsctx->processing;
    else if (state == FLUX_JOB_DEPEND
             || state == FLUX_JOB_SCHED)
        return jsctx->pending;
    else if (state == FLUX_JOB_RUN
             || state == FLUX_JOB_CLEANUP)
        return jsctx->running;
    else /* state == FLUX_JOB_INACTIVE */
        return jsctx->inactive;
}

static void update_job_state_and_list (struct info_ctx *ctx,
                                       struct job *job,
                                       flux_job_state_t newstate,
                                       double timestamp)
{
    zlistx_t *oldlist, *newlist;
    struct job_state_ctx *jsctx = job->ctx->jsctx;

    oldlist = get_list (jsctx, job->state);
    newlist = get_list (jsctx, newstate);

    /* must call before job_change_list(), to ensure timestamps are
     * set before any sorting based on timestamps are done
     */
    update_job_state (ctx, job, newstate, timestamp);

    if (oldlist != newlist)
        job_change_list (jsctx, job, oldlist, newstate);
}

static void list_id_respond (struct info_ctx *ctx,
                             struct idsync_data *isd,
                             struct job *job)
{
    job_info_error_t err;
    json_t *o;

    if (!(o = job_to_json (job, isd->attrs, &err)))
        goto error;

    if (flux_respond_pack (ctx->h, isd->msg, "{s:O}", "job", o) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    json_decref (o);
    return;

error:
    if (flux_respond_error (ctx->h, isd->msg, errno, err.text) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (o);
}

static void check_waiting_id (struct info_ctx *ctx,
                              struct job *job)
{
    zlistx_t *list_isd;

    if ((list_isd = zhashx_lookup (ctx->idsync_waits, &job->id))) {
        struct idsync_data *isd;
        isd = zlistx_first (list_isd);
        while (isd) {
            list_id_respond (ctx, isd, job);
            isd = zlistx_next (list_isd);
        }
        zhashx_delete (ctx->idsync_waits, &job->id);
    }
}

static int eventlog_lookup_parse (struct info_ctx *ctx,
                                  struct job *job,
                                  const char *s)
{
    json_t *a = NULL;
    size_t index;
    json_t *value;
    int rc = -1;

    if (!(a = eventlog_decode (s))) {
        flux_log_error (ctx->h, "%s: error parsing eventlog for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto nonfatal_error;
    }

    json_array_foreach (a, index, value) {
        const char *name;
        double timestamp;
        json_t *context = NULL;

        if (eventlog_entry_parse (value, &timestamp, &name, &context) < 0) {
            flux_log_error (ctx->h, "%s: error parsing entry for %ju",
                            __FUNCTION__, (uintmax_t)job->id);
            continue;
        }

        if (!strcmp (name, "submit")) {
            if (!context) {
                flux_log (ctx->h, LOG_ERR, "%s: no submit context for %ju",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }

            if (json_unpack (context, "{ s:i s:i s:i }",
                             "priority", &job->priority,
                             "userid", &job->userid,
                             "flags", &job->flags) < 0) {
                flux_log (ctx->h, LOG_ERR, "%s: submit context for %ju invalid",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }
            job->priority_timestamp = timestamp;
        }
        else if (!strcmp (name, "priority")) {
            if (!context) {
                flux_log (ctx->h, LOG_ERR, "%s: no priority context for %ju",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }

            if (json_unpack (context, "{ s:i }",
                             "priority", &job->priority) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: priority context for %ju invalid",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }
            job->priority_timestamp = timestamp;
        }
    }

    /* nonfatal error - eventlog illegal, but we'll continue on.  job
     * listing will get initialized data */
nonfatal_error:
    rc = 0;
    json_decref (a);
    return rc;
}

struct res_level {
    const char *type;
    int count;
    json_t *with;
};

static int parse_res_level (struct info_ctx *ctx,
                            struct job *job,
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
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        return -1;
    }
    *resp = res;
    return 0;
}

/* Return basename of path if there is a '/' in path.  Otherwise return
 * full path */
const char *
parse_job_name (const char *path)
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

static int jobspec_parse (struct info_ctx *ctx,
                          struct job *job,
                          const char *s)
{
    json_error_t error;
    json_t *jobspec = NULL;
    json_t *tasks, *resources, *command, *jobspec_job = NULL;
    int rc = -1;

    if (!(jobspec = json_loads (s, 0, &error))) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto error;
    }

    if (json_unpack_ex (jobspec, &error, 0,
                        "{s:{s:{s?:o}}}",
                        "attributes",
                        "system",
                        "job",
                        &jobspec_job) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (jobspec_job) {
        if (!json_is_object (jobspec_job)) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec",
                      __FUNCTION__, (uintmax_t)job->id);
            goto nonfatal_error;
        }
        job->jobspec_job = json_incref (jobspec_job);
    }

    if (json_unpack_ex (jobspec, &error, 0,
                        "{s:o}",
                        "tasks", &tasks) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }
    if (json_unpack_ex (tasks, &error, 0,
                        "[{s:o}]",
                        "command", &command) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (!json_is_array (command)) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec",
                  __FUNCTION__, (uintmax_t)job->id);
        goto nonfatal_error;
    }

    job->jobspec_cmd = json_incref (command);

    if (job->jobspec_job) {
        if (json_unpack_ex (job->jobspec_job, &error, 0,
                            "{s?:s}",
                            "name", &job->name) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid job dictionary: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            goto nonfatal_error;
        }
    }

    /* If user did not specify job.name, we treat arg 0 of the command
     * as the job name */
    if (!job->name) {
        json_t *arg0 = json_array_get (job->jobspec_cmd, 0);
        if (!arg0 || !json_is_string (arg0)) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid job command",
                      __FUNCTION__, (uintmax_t)job->id);
            goto nonfatal_error;
        }
        job->name = parse_job_name (json_string_value (arg0));
        assert (job->name);
    }

    if (json_unpack_ex (jobspec, &error, 0,
                        "{s:o}",
                        "resources", &resources) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid jobspec: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    /* Set job->ntasks
     */
    if (json_unpack_ex (tasks, NULL, 0,
                        "[{s:{s:i}}]",
                        "count", "total", &job->ntasks) < 0) {
        int per_slot, slot_count = 0;
        struct res_level res[3];

        if (json_unpack_ex (tasks, NULL, 0,
                            "[{s:{s:i}}]",
                            "count", "per_slot", &per_slot) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju invalid jobspec: %s",
                      __FUNCTION__, (uintmax_t)job->id, error.text);
            goto nonfatal_error;
        }
        if (per_slot != 1) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju: per_slot count: expected 1 got %d: %s",
                      __FUNCTION__, (uintmax_t)job->id, per_slot,
                      error.text);
            goto nonfatal_error;
        }
        /* For jobspec version 1, expect either:
         * - node->slot->core->NIL
         * - slot->core->NIL
         * Set job->slot_count and job->cores_per_slot.
         */
        memset (res, 0, sizeof (res));
        if (parse_res_level (ctx, job, resources, &res[0]) < 0)
            goto nonfatal_error;
        if (res[0].with && parse_res_level (ctx, job, res[0].with, &res[1]) < 0)
            goto nonfatal_error;
        if (res[1].with && parse_res_level (ctx, job, res[1].with, &res[2]) < 0)
            goto nonfatal_error;
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
            flux_log (ctx->h, LOG_WARNING,
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
     * listing will get initialized data */
nonfatal_error:
    rc = 0;
error:
    json_decref (jobspec);
    return rc;
}

static void state_depend_lookup_continuation (flux_future_t *f, void *arg)
{
    struct job *job = arg;
    struct info_ctx *ctx = job->ctx;
    struct state_transition *st;
    const char *s;
    void *handle;

    if (flux_rpc_get_unpack (f, "{s:s}", "eventlog", &s) < 0) {
        flux_log_error (ctx->h, "%s: error eventlog for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto out;
    }

    if (eventlog_lookup_parse (ctx, job, s) < 0)
        goto out;

    if (flux_rpc_get_unpack (f, "{s:s}", "jobspec", &s) < 0) {
        flux_log_error (ctx->h, "%s: error jobspec for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto out;
    }

    if (jobspec_parse (ctx, job, s) < 0)
        goto out;

    st = zlist_head (job->next_states);
    assert (st);
    update_job_state_and_list (ctx, job, st->state, st->timestamp);
    check_waiting_id (ctx, job);
    zlist_remove (job->next_states, st);
    process_next_state (ctx, job);

out:
    handle = zlistx_find (ctx->jsctx->futures, f);
    if (handle)
        zlistx_detach (ctx->jsctx->futures, handle);
    flux_future_destroy (f);
}

static flux_future_t *state_depend_lookup (struct job_state_ctx *jsctx,
                                           struct job *job)
{
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(f = flux_rpc_pack (jsctx->h, "job-info.lookup", FLUX_NODEID_ANY, 0,
                             "{s:I s:[ss] s:i}",
                             "id", job->id,
                             "keys", "eventlog", "jobspec",
                             "flags", 0))) {
        flux_log_error (jsctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (f, -1, state_depend_lookup_continuation, job) < 0) {
        flux_log_error (jsctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    return f;

 error:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return NULL;
}

static int idset_add_set (struct idset *set, struct idset *new)
{
    unsigned int i = idset_first (new);
    while (i != IDSET_INVALID_ID) {
        if (idset_test (set, i)) {
            errno = EEXIST;
            return -1;
        }
        if (idset_set (set, i) < 0)
            return -1;
        i = idset_next (new, i);
    }
    return 0;
}

static int idset_set_string (struct idset *idset, const char *ids)
{
    int rc;
    struct idset *new = idset_decode (ids);
    if (!new)
        return -1;
    rc = idset_add_set (idset, new);
    idset_destroy (new);
    return rc;
}

static int parse_rlite (struct info_ctx *ctx,
                        struct job *job,
                        const json_t *R_lite)
{
    struct idset *idset = NULL;
    size_t index;
    json_t *value;
    int saved_errno, rc = -1;
    int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;

    if (!(idset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return -1;
    json_array_foreach (R_lite, index, value) {
        const char *ranks = NULL;
        if ((json_unpack_ex (value, NULL, 0, "{s:s}", "rank", &ranks) < 0)
            || (idset_set_string (idset, ranks) < 0))
            goto nonfatal_err;
    }

    job->nnodes = idset_count (idset);
    if (!(job->ranks = idset_encode (idset, flags)))
        goto nonfatal_err;

    /* nonfatal error - invalid rlite, but we'll continue on.  job
     * listing will get initialized data */
nonfatal_err:
    rc = 0;
    saved_errno = errno;
    idset_destroy (idset);
    errno = saved_errno;
    return rc;
}

static int R_lookup_parse (struct info_ctx *ctx,
                           struct job *job,
                           const char *s)
{
    json_error_t error;
    json_t *R_lite = NULL;
    int version;
    int rc = -1;

    if (!(job->R = json_loads (s, 0, &error))) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid R: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }

    if (json_unpack_ex (job->R, &error, 0,
                        "{s:i s:{s?F s:o}}",
                        "version", &version,
                        "execution",
                        "expiration", &job->expiration,
                        "R_lite", &R_lite) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid R: %s",
                  __FUNCTION__, (uintmax_t)job->id, error.text);
        goto nonfatal_error;
    }
    if (version != 1) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju invalid R version: %d",
                  __FUNCTION__, (uintmax_t)job->id, version);
        goto nonfatal_error;
    }
    if (parse_rlite (ctx, job, R_lite) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju parse_rlite: %s",
                  __FUNCTION__, (uintmax_t)job->id, strerror (errno));
        goto error;
    }

    /* nonfatal error - invalid rlite, but we'll continue on.  job
     * listing will get initialized data */
nonfatal_error:
    rc = 0;
error:
    return rc;
}

static void state_run_lookup_continuation (flux_future_t *f, void *arg)
{
    struct job *job = arg;
    struct info_ctx *ctx = job->ctx;
    struct state_transition *st;
    const char *s;
    void *handle;

    if (flux_rpc_get_unpack (f, "{s:s}", "R", &s) < 0) {
        flux_log_error (ctx->h, "%s: error eventlog for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto out;
    }

    if (R_lookup_parse (ctx, job, s) < 0)
        goto out;

    st = zlist_head (job->next_states);
    assert (st);
    update_job_state_and_list (ctx, job, st->state, st->timestamp);
    zlist_remove (job->next_states, st);
    process_next_state (ctx, job);

out:
    handle = zlistx_find (ctx->jsctx->futures, f);
    if (handle)
        zlistx_detach (ctx->jsctx->futures, handle);
    flux_future_destroy (f);
}

static flux_future_t *state_run_lookup (struct job_state_ctx *jsctx,
                                           struct job *job)
{
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(f = flux_rpc_pack (jsctx->h, "job-info.lookup", FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", job->id,
                             "keys", "R",
                             "flags", 0))) {
        flux_log_error (jsctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (f, -1, state_run_lookup_continuation, job) < 0) {
        flux_log_error (jsctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    return f;

 error:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return NULL;
}

static int eventlog_inactive_parse (struct info_ctx *ctx,
                                    struct job *job,
                                    const char *s)
{
    json_t *a = NULL;
    size_t index;
    json_t *value;
    int rc = -1;

    if (!(a = eventlog_decode (s))) {
        flux_log (ctx->h, LOG_ERR,
                  "%s: job %ju eventlog_decode: %s",
                  __FUNCTION__, (uintmax_t)job->id, strerror (errno));
        goto nonfatal_error;
    }

    json_array_foreach (a, index, value) {
        const char *name = NULL;
        json_t *context = NULL;

        if (eventlog_entry_parse (value, NULL, &name, &context) < 0) {
            flux_log (ctx->h, LOG_ERR,
                      "%s: job %ju eventlog_entry_parse: %s",
                      __FUNCTION__, (uintmax_t)job->id,
                      strerror (errno));
            continue;
        }

        /* There is no need to check for "exception" events for the
         * "success" attribute.  "success" is always false unless the
         * job completes ("finish") without error.
         */
        if (!strcmp (name, "finish")) {
            int status;
            if (json_unpack (context, "{s:i}", "status", &status) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju parse finish status",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }
            if (!status)
                job->success = true;
        }
        else if (!strcmp (name, "exception")) {
            const char *type;
            int severity;
            const char *note = NULL;

            if (json_unpack (context,
                             "{s:s s:i s?:s}",
                             "type", &type,
                             "severity", &severity,
                             "note", &note) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: job %ju parse exception",
                          __FUNCTION__, (uintmax_t)job->id);
                goto nonfatal_error;
            }
            if (!job->exception_occurred
                || severity < job->exception_severity) {
                job->exception_occurred = true;
                job->exception_severity = severity;
                job->exception_type = type;
                job->exception_note = note;
                json_decref (job->exception_context);
                job->exception_context = json_incref (context);
            }
        }
    }

    /* nonfatal error - eventlog illegal, but we'll continue on.  job
     * listing will get initialized data */
nonfatal_error:
    rc = 0;
    json_decref (a);
    return rc;
}

/* calculate any remaining fields */
static int eventlog_inactive_finish (struct info_ctx *ctx,
                                     struct job *job)
{
    /* Default result is failed, overridden below */
    job->result = FLUX_JOB_RESULT_FAILED;
    if (job->success)
        job->result = FLUX_JOB_RESULT_COMPLETED;
    else if (job->exception_occurred) {
        if (!strcmp (job->exception_type, "cancel"))
            job->result = FLUX_JOB_RESULT_CANCELLED;
        else if (!strcmp (job->exception_type, "timeout"))
            job->result = FLUX_JOB_RESULT_TIMEOUT;
    }
    return 0;
}

static void state_inactive_lookup_continuation (flux_future_t *f, void *arg)
{
    struct job *job = arg;
    struct info_ctx *ctx = job->ctx;
    struct state_transition *st;
    const char *s;
    void *handle;

    if (flux_rpc_get_unpack (f, "{s:s}", "eventlog", &s) < 0) {
        flux_log_error (ctx->h, "%s: error eventlog for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto out;
    }

    if (eventlog_inactive_parse (ctx, job, s) < 0)
        goto out;

    if (eventlog_inactive_finish (ctx, job) < 0)
        goto out;

    st = zlist_head (job->next_states);
    assert (st);
    update_job_state_and_list (ctx, job, st->state, st->timestamp);
    zlist_remove (job->next_states, st);
    process_next_state (ctx, job);

out:
    handle = zlistx_find (ctx->jsctx->futures, f);
    if (handle)
        zlistx_detach (ctx->jsctx->futures, handle);
    flux_future_destroy (f);
}

static flux_future_t *state_inactive_lookup (struct job_state_ctx *jsctx,
                                           struct job *job)
{
    flux_future_t *f = NULL;

    if (!(f = flux_rpc_pack (jsctx->h, "job-info.lookup", FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", job->id,
                             "keys", "eventlog",
                             "flags", 0))) {
        flux_log_error (jsctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (f, -1, state_inactive_lookup_continuation, job) < 0) {
        flux_log_error (jsctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    return f;

 error:
    flux_future_destroy (f);
    return NULL;
}

static void state_transition_destroy (void *data)
{
    struct state_transition *st = data;
    if (st)
        free (st);
}

static int add_state_transition (struct job *job,
                                 flux_job_state_t newstate,
                                 double timestamp)
{
    struct state_transition *st = NULL;
    int saved_errno;

    if (!(st = calloc (1, sizeof (*st))))
        return -1;
    st->state = newstate;
    st->processed = false;
    st->timestamp = timestamp;

    if (zlist_append (job->next_states, st) < 0) {
        errno = ENOMEM;
        goto cleanup;
    }
    zlist_freefn (job->next_states, st, state_transition_destroy, true);

    return 0;

 cleanup:
    saved_errno = errno;
    state_transition_destroy (st);
    errno = saved_errno;
    return -1;
}

static void process_next_state (struct info_ctx *ctx, struct job *job)
{
    struct state_transition *st;
    struct job_state_ctx *jsctx = job->ctx->jsctx;

    while ((st = zlist_head (job->next_states))
           && !st->processed) {
        if (st->state == FLUX_JOB_DEPEND
            || st->state == FLUX_JOB_RUN
            || st->state == FLUX_JOB_INACTIVE) {
            flux_future_t *f = NULL;

            if (st->state == FLUX_JOB_DEPEND) {
                /* get initial job information, such as userid,
                 * priority, t_submit, flags, and jobspec info */
                if (!(f = state_depend_lookup (jsctx, job))) {
                    flux_log_error (jsctx->h, "%s: state_depend_lookup", __FUNCTION__);
                    return;
                }
            }
            else if (st->state == FLUX_JOB_RUN) {
                /* get R to get node count, etc. */
                if (!(f = state_run_lookup (jsctx, job))) {
                    flux_log_error (jsctx->h, "%s: state_run_lookup", __FUNCTION__);
                    return;
                }
            }
            else { /* st->state == FLUX_JOB_INACTIVE */
                /* get eventlog to success=true|false */
                if (!(f = state_inactive_lookup (jsctx, job))) {
                    flux_log_error (jsctx->h, "%s: state_inactive_lookup",
                                    __FUNCTION__);
                    return;
                }
            }

            if (!zlistx_add_end (jsctx->futures, f)) {
                flux_log_error (jsctx->h, "%s: zlistx_add_end", __FUNCTION__);
                flux_future_destroy (f);
                return;
            }

            st->processed = true;
            break;
        }
        else {
            /* FLUX_JOB_SCHED */
            /* FLUX_JOB_CLEANUP */
            update_job_state_and_list (ctx, job, st->state, st->timestamp);
            zlist_remove (job->next_states, st);
        }
    }
}

static int parse_transition (json_t *transition, flux_jobid_t *id,
                             flux_job_state_t *state, double *timestamp)
{
    json_t *o;

    if (!json_is_array (transition))
        return -1;

    if (!(o = json_array_get (transition, 0))
        || !json_is_integer (o))
        return -1;

    (*id) = json_integer_value (o);

    if (!(o = json_array_get (transition, 1))
        || !json_is_string (o))
        return -1;

    if (flux_job_strtostate (json_string_value (o), state) < 0)
        return -1;

    if (!(o = json_array_get (transition, 2))
        || !json_is_real (o))
        return -1;

    (*timestamp) = json_real_value (o);
    return 0;
}

static void update_jobs (struct info_ctx *ctx, json_t *transitions)
{
    struct job_state_ctx *jsctx = ctx->jsctx;
    size_t index;
    json_t *value;

    if (!json_is_array (transitions)) {
        flux_log (ctx->h, LOG_ERR, "%s: transitions EPROTO", __FUNCTION__);
        return;
    }

    json_array_foreach (transitions, index, value) {
        struct job *job;
        flux_jobid_t id;
        flux_job_state_t state;
        double timestamp;

        if (parse_transition (value, &id, &state, &timestamp) < 0) {
            flux_log (jsctx->h, LOG_ERR, "%s: transition EPROTO", __FUNCTION__);
            return;
        }

        if (!(job = zhashx_lookup (jsctx->index, &id))) {
            struct annotations_data *ad;
            if (!(job = job_create (ctx, id))){
                flux_log_error (jsctx->h, "%s: job_create", __FUNCTION__);
                return;
            }
            if (zhashx_insert (jsctx->index, &job->id, job) < 0) {
                flux_log_error (jsctx->h, "%s: zhashx_insert", __FUNCTION__);
                job_destroy (job);
                return;
            }
            /* in rare case, annotation may have arrived before we
             * knew of this job */
            if ((ad = zhashx_lookup (jsctx->early_annotations, &id))) {
                job->annotations_timestamp = ad->timestamp;
                if (ad->annotations)
                    job->annotations = json_incref (ad->annotations);
                zhashx_delete (jsctx->early_annotations, &id);
            }
            /* job always starts off on processing list */
            if (!(job->list_handle = zlistx_add_end (jsctx->processing, job))) {
                flux_log_error (jsctx->h, "%s: zlistx_add_end", __FUNCTION__);
                return;
            }
        }

        if (add_state_transition (job, state, timestamp) < 0) {
            flux_log_error (jsctx->h, "%s: add_state_transition",
                            __FUNCTION__);
            return;
        }

        process_next_state (ctx, job);
    }

}

void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                   const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    json_t *transitions;

    if (ctx->jsctx->pause) {
        flux_msg_t *cpy;

        if (!(cpy = flux_msg_copy (msg, true))) {
            flux_log_error (h, "%s: flux_msg_copy", __FUNCTION__);
            return;
        }
        if (!zlistx_add_end (ctx->jsctx->transitions, cpy)) {
            flux_log_error (h, "%s: zlistx_add_end", __FUNCTION__);
            flux_msg_destroy (cpy);
        }
    }
    else {
        if (flux_event_unpack (msg, NULL, "{s:o}",
                               "transitions",
                               &transitions) < 0) {
            flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
            return;
        }

        update_jobs (ctx, transitions);
    }

    return;
}

void job_state_pause_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;

    ctx->jsctx->pause = true;

    if (flux_respond (h, msg, NULL) < 0) {
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
        goto error;
    }

    return;

 error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void job_state_unpause_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    flux_msg_t *tmsg;

    ctx->jsctx->pause = false;

    tmsg = zlistx_first (ctx->jsctx->transitions);
    while (tmsg) {
        job_state_cb (h, mh, tmsg, ctx);
        tmsg = zlistx_next (ctx->jsctx->transitions);
    }

    if (flux_respond (h, msg, NULL) < 0) {
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
        goto error;
    }

    zlistx_purge (ctx->jsctx->transitions);
    return;

 error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    zlistx_purge (ctx->jsctx->transitions);
}

static struct job *eventlog_restart_parse (struct info_ctx *ctx,
                                           const char *eventlog,
                                           flux_jobid_t id)
{
    struct job *job = NULL;
    json_t *a = NULL;
    size_t index;
    json_t *value;

    if (!(job = job_create (ctx, id)))
        goto error;

    if (!(a = eventlog_decode (eventlog))) {
        flux_log_error (ctx->h, "%s: error parsing eventlog for %ju",
                        __FUNCTION__, (uintmax_t)job->id);
        goto error;
    }

    json_array_foreach (a, index, value) {
        const char *name;
        double timestamp;
        json_t *context = NULL;

        if (eventlog_entry_parse (value, &timestamp, &name, &context) < 0) {
            flux_log_error (ctx->h, "%s: error parsing entry for %ju",
                            __FUNCTION__, (uintmax_t)job->id);
            goto error;
        }

        if (!strcmp (name, "submit")) {
            if (!context) {
                flux_log (ctx->h, LOG_ERR, "%s: no submit context for %ju",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }

            if (json_unpack (context, "{ s:i s:i s:i }",
                             "priority", &job->priority,
                             "userid", &job->userid,
                             "flags", &job->flags) < 0) {
                flux_log (ctx->h, LOG_ERR, "%s: submit context for %ju invalid",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }
            job->priority_timestamp = timestamp;
            update_job_state (ctx, job, FLUX_JOB_DEPEND, timestamp);
        }
        else if (!strcmp (name, "depend")) {
            update_job_state (ctx, job, FLUX_JOB_SCHED, timestamp);
        }
        else if (!strcmp (name, "priority")) {
            if (!context) {
                flux_log (ctx->h, LOG_ERR, "%s: no priority context for %ju",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }

            if (json_unpack (context, "{ s:i }",
                                      "priority", &job->priority) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: priority context for %ju invalid",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }
            job->priority_timestamp = timestamp;
        }
        else if (!strcmp (name, "exception")) {
            int severity;
            if (!context) {
                flux_log (ctx->h, LOG_ERR, "%s: no exception context for %ju",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }

            if (json_unpack (context, "{ s:i }", "severity", &severity) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "%s: exception context for %ju invalid",
                          __FUNCTION__, (uintmax_t)job->id);
                goto error;
            }
            if (severity == 0)
                update_job_state (ctx, job, FLUX_JOB_CLEANUP, timestamp);
        }
        else if (!strcmp (name, "alloc")) {
            /* context not required if no annotations */
            if (context) {
                json_t *annotations;
                if (!(annotations = json_object_get (context, "annotations"))) {
                    flux_log (ctx->h, LOG_ERR,
                              "%s: alloc context for %ju invalid",
                              __FUNCTION__, (uintmax_t)job->id);
                    goto error;
                }
                job->annotations = json_incref (annotations);
            }

            if (job->state == FLUX_JOB_SCHED)
                update_job_state (ctx, job, FLUX_JOB_RUN, timestamp);
        }
        else if (!strcmp (name, "finish")) {
            if (job->state == FLUX_JOB_RUN)
                update_job_state (ctx, job, FLUX_JOB_CLEANUP, timestamp);
        }
        else if (!strcmp (name, "clean")) {
            update_job_state (ctx, job, FLUX_JOB_INACTIVE, timestamp);
        }
    }

    if (job->state == FLUX_JOB_NEW) {
        flux_log (ctx->h, LOG_ERR, "%s: eventlog has no transition events",
                  __FUNCTION__);
        goto error;
    }

    json_decref (a);
    return job;

error:
    job_destroy (job);
    json_decref (a);
    return NULL;
}

static int depthfirst_count_depth (const char *s)
{
    int count = 0;
    while (*s) {
        if (*s++ == '.')
            count++;
    }
    return count;
}

static int depthfirst_map_one (struct info_ctx *ctx, const char *key,
                               int dirskip)
{
    struct job *job = NULL;
    flux_jobid_t id;
    flux_future_t *f1 = NULL;
    flux_future_t *f2 = NULL;
    flux_future_t *f3 = NULL;
    const char *eventlog, *jobspec, *R;
    char path[64];
    int rc = -1;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    if (flux_job_kvs_key (path, sizeof (path), id, "eventlog") < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(f1 = flux_kvs_lookup (ctx->h, NULL, 0, path)))
        goto done;
    if (flux_kvs_lookup_get (f1, &eventlog) < 0)
        goto done;

    if (!(job = eventlog_restart_parse (ctx, eventlog, id)))
        goto done;

    if (flux_job_kvs_key (path, sizeof (path), id, "jobspec") < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!(f2 = flux_kvs_lookup (ctx->h, NULL, 0, path)))
        goto done;
    if (flux_kvs_lookup_get (f2, &jobspec) < 0)
        goto done;

    if (jobspec_parse (ctx, job, jobspec) < 0)
        goto done;

    if (job->states_mask & FLUX_JOB_RUN) {
        if (flux_job_kvs_key (path, sizeof (path), id, "R") < 0) {
            errno = EINVAL;
            return -1;
        }
        if (!(f3 = flux_kvs_lookup (ctx->h, NULL, 0, path)))
            goto done;
        if (flux_kvs_lookup_get (f3, &R) < 0)
            goto done;

        if (R_lookup_parse (ctx, job, R) < 0)
            goto done;
    }

    if (job->states_mask & FLUX_JOB_INACTIVE) {
        if (eventlog_inactive_parse (ctx, job, eventlog) < 0)
            goto done;

        if (eventlog_inactive_finish (ctx, job) < 0)
            goto done;
    }

    if (zhashx_insert (ctx->jsctx->index, &job->id, job) < 0) {
        flux_log_error (ctx->h, "%s: zhashx_insert", __FUNCTION__);
        goto done;
    }
    job_insert_list (ctx->jsctx, job, job->state);

    rc = 1;
done:
    if (rc < 0)
        job_destroy (job);
    flux_future_destroy (f1);
    flux_future_destroy (f2);
    flux_future_destroy (f3);
    return rc;
}

static int depthfirst_map (struct info_ctx *ctx, const char *key,
                           int dirskip)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = depthfirst_count_depth (key + dirskip);
    if (!(f = flux_kvs_lookup (ctx->h, NULL, FLUX_KVS_READDIR, key)))
        return -1;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && path_level == 0)
            rc = 0;
        goto done;
    }
    if (!(itr = flux_kvsitr_create (dir)))
        goto done;
    while ((name = flux_kvsitr_next (itr))) {
        char *nkey;
        int n;
        if (!flux_kvsdir_isdir (dir, name))
            continue;
        if (!(nkey = flux_kvsdir_key_at (dir, name)))
            goto done_destroyitr;
        if (path_level == 3) // orig 'key' = .A.B.C, thus 'nkey' is complete
            n = depthfirst_map_one (ctx, nkey, dirskip);
        else
            n = depthfirst_map (ctx, nkey, dirskip);
        if (n < 0) {
            int saved_errno = errno;
            free (nkey);
            errno = saved_errno;
            goto done_destroyitr;
        }
        count += n;
        free (nkey);
    }
    rc = count;
done_destroyitr:
    flux_kvsitr_destroy (itr);
done:
    flux_future_destroy (f);
    return rc;
}

/* Read jobs present in the KVS at startup. */
int job_state_init_from_kvs (struct info_ctx *ctx)
{
    const char *dirname = "job";
    int dirskip = strlen (dirname);
    int count;

    count = depthfirst_map (ctx, dirname, dirskip);
    if (count < 0)
        return -1;
    flux_log (ctx->h, LOG_DEBUG, "%s: read %d jobs", __FUNCTION__, count);

    zlistx_sort (ctx->jsctx->running);
    zlistx_sort (ctx->jsctx->inactive);
    return 0;
}

static int job_events_priority (struct job_state_ctx *jsctx,
                                flux_jobid_t id,
                                double timestamp,
                                json_t *context)
{
    struct job *job;
    int priority;

    if (!context
        || json_unpack (context, "{ s:i }", "priority", &priority) < 0
        || (priority < FLUX_JOB_PRIORITY_MIN
            || priority > FLUX_JOB_PRIORITY_MAX)) {
        flux_log (jsctx->h, LOG_ERR, "%s: priority context invalid",
                  __FUNCTION__);
        return -1;
    }

    /* if we do not yet know about this job via the job state
     * transitions, no need to update.  It'll be handled on
     * initial reading of data from KVS
     */
    if ((job = zhashx_lookup (jsctx->index, &id))) {
        if (job->priority_timestamp > 0.0
            && job->priority_timestamp < timestamp) {
            int orig_priority = job->priority;
            job->priority = priority;
            job->priority_timestamp = timestamp;

            if (job->state & FLUX_JOB_PENDING
                && job->priority != orig_priority)
                zlistx_reorder (jsctx->pending,
                                job->list_handle,
                                search_direction (job));
        }
    }

    return 0;
}

static struct annotations_data *annotations_data_create (void)
{
    struct annotations_data *ad = calloc (1, sizeof (*ad));
    if (!ad)
        return NULL;
    return ad;
}

static void annotations_data_destroy (void *data)
{
    if (data) {
        struct annotations_data *ad = data;
        json_decref (ad->annotations);
        free (ad);
    }
}

static void annotations_data_destroy_wrapper (void **data)
{
    struct annotations_data **ad = (struct annotations_data **)data;
    annotations_data_destroy (*ad);
}

static int update_early_annotations (struct job_state_ctx *jsctx,
                                     flux_jobid_t id,
                                     double timestamp,
                                     json_t *annotations)
{
    struct annotations_data *ad = NULL;

    if (!(ad = zhashx_lookup (jsctx->early_annotations, &id))) {
        if (!(ad = annotations_data_create ())) {
            flux_log_error (jsctx->h, "%s: annotations_data_create",
                            __FUNCTION__);
            goto error;
        }
        if (zhashx_insert (jsctx->early_annotations,
                           &id,
                           ad) < 0) {
            flux_log_error (jsctx->h, "%s: zhashx_insert", __FUNCTION__);
            goto error;
        }
    }
    if (timestamp > ad->timestamp) {
        json_decref (ad->annotations);
        if (json_is_null (annotations))
            ad->annotations = NULL;
        else
            ad->annotations = json_incref (annotations);
        ad->timestamp = timestamp;
    }

    return 0;

error:
    annotations_data_destroy (ad);
    return -1;
}

static int job_events_annotations (struct job_state_ctx *jsctx,
                                   flux_jobid_t id,
                                   double timestamp,
                                   json_t *context)
{
    struct job *job;
    json_t *annotations;

    if (!context
        || json_unpack (context, "{ s:o }", "annotations", &annotations) < 0) {
        flux_log (jsctx->h, LOG_ERR, "%s: annotations event context invalid",
                  __FUNCTION__);
        return -1;
    }

    if ((job = zhashx_lookup (jsctx->index, &id))) {
        if (timestamp > job->annotations_timestamp) {
            json_decref (job->annotations);
            if (json_is_null (annotations))
                job->annotations = NULL;
            else
                job->annotations = json_incref (annotations);
            job->annotations_timestamp = timestamp;
        }
    }
    else {
        /* annotation event may have arrived before job-state
         * event indicating job existence, store off for later.
         */
        if (update_early_annotations (jsctx, id, timestamp, annotations) < 0)
            return -1;
    }

    return 0;
}

static void job_events_continuation (flux_future_t *f, void *arg)
{
    struct job_state_ctx *jsctx = arg;
    size_t index;
    json_t *value;
    json_t *events;

    if (flux_rpc_get_unpack (f, "{s:o}", "events", &events) < 0) {
        flux_log_error (jsctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    if (!json_is_array (events)) {
        flux_log (jsctx->h, LOG_ERR, "%s: events EPROTO", __FUNCTION__);
        goto error;
    }

    json_array_foreach (events, index, value) {
        flux_jobid_t id;
        json_t *entry;
        const char *name;
        double timestamp;
        json_t *context = NULL;

        if (json_unpack (value, "{s:I s:o}", "id", &id, "entry", &entry) < 0
            || eventlog_entry_parse (entry, &timestamp, &name, &context) < 0) {
            flux_log (jsctx->h, LOG_ERR, "%s: error parsing record",
                      __FUNCTION__);
            goto error;
        }

        if (!strcmp (name, "priority")) {
            if (job_events_priority (jsctx, id, timestamp, context) < 0)
                goto error;
        }
        else if (!strcmp (name, "annotations")) {
            if (job_events_annotations (jsctx, id, timestamp, context) < 0)
                goto error;
        }
    }

    flux_future_reset (f);
    return;

error:
    /* future will be cleaned up in shutdown path */
    flux_reactor_stop_error (flux_get_reactor (jsctx->h));
    return;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
