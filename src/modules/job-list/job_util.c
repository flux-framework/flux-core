/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_util.c - job utility functions */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <stdarg.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "job-list.h"
#include "job_util.h"

static int store_attr (struct job *job,
                       const char *attr,
                       json_t *o,
                       flux_error_t *errp)
{
    json_t *val = NULL;

    if (streq (attr, "userid")) {
        val = json_integer (job->userid);
    }
    else if (streq (attr, "urgency")) {
        val = json_integer (job->urgency);
    }
    else if (streq (attr, "priority")) {
        if (!(job->states_mask & FLUX_JOB_STATE_SCHED))
            return 0;
        val = json_integer (job->priority);
    }
    else if (streq (attr, "t_submit")) {
        val = json_real (job->t_submit);
    }
    else if (streq (attr, "t_depend")) {
        /* if submit_version < 1, it means it was not set.  This is
         * before the introduction of event `validate` after 0.41.1.
         * Before the introduction of this event, t_submit and
         * t_depend are the same.  So return the value of t_submit
         */
        if (job->submit_version < 1) {
            val = json_real (job->t_submit);
            goto out;
        }
        if (!(job->states_mask & FLUX_JOB_STATE_DEPEND))
            return 0;
        val = json_real (job->t_depend);
    }
    else if (streq (attr, "t_run")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        val = json_real (job->t_run);
    }
    else if (streq (attr, "t_cleanup")) {
        if (!(job->states_mask & FLUX_JOB_STATE_CLEANUP))
            return 0;
        val = json_real (job->t_cleanup);
    }
    else if (streq (attr, "t_inactive")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_real (job->t_inactive);
    }
    else if (streq (attr, "state")) {
        val = json_integer (job->state);
    }
    else if (streq (attr, "name")) {
        /* job->name potentially NULL if jobspec invalid */
        if (!job->name)
            return 0;
        val = json_string (job->name);
    }
    else if (streq (attr, "cwd")) {
        /* job->cwd potentially NULL, is optional in jobspec */
        if (!job->cwd)
            return 0;
        val = json_string (job->cwd);
    }
    else if (streq (attr, "queue")) {
        /* job->queue potentially NULL if:
         * - unspecified
         * - jobspec invalid
         */
        if (!job->queue)
            return 0;
        val = json_string (job->queue);
    }
    else if (streq (attr, "project")) {
        /* job->project potentially NULL, usually set via jobspec-update event */
        if (!job->project)
            return 0;
        val = json_string (job->project);
    }
    else if (streq (attr, "bank")) {
        /* job->bank potentially NULL, usually set via jobspec-update event */
        if (!job->bank)
            return 0;
        val = json_string (job->bank);
    }
    else if (streq (attr, "ntasks")) {
        /* job->ntasks potentially < 0 if jobspec invalid */
        if (job->ntasks < 0)
            return 0;
        val = json_integer (job->ntasks);
    }
    else if (streq (attr, "ncores")) {
        /* job->ncores potentially < 0 if not set yet or R invalid,
         * may be set in DEPEND or RUN state */
        if (job->ncores < 0)
            return 0;
        val = json_integer (job->ncores);
    }
    else if (streq (attr, "duration")) {
        /* job->duration potentially < 0 if jobspec invalid */
        if (job->duration < 0)
            return 0;
        val = json_real (job->duration);
    }
    else if (streq (attr, "nnodes")) {
        /* job->nnodes < 0 if not set yet or R invalid, may be set in
         * DEPEND or RUN state */
        if (job->nnodes < 0)
            return 0;
        val = json_integer (job->nnodes);
    }
    else if (streq (attr, "ranks")) {
        /* job->ranks potentially NULL if R invalid */
        if (!(job->states_mask & FLUX_JOB_STATE_RUN)
            || !job->ranks)
            return 0;
        val = json_string (job->ranks);
    }
    else if (streq (attr, "nodelist")) {
        /* job->nodelist potentially NULL if R invalid */
        if (!(job->states_mask & FLUX_JOB_STATE_RUN)
            || !job->nodelist)
            return 0;
        val = json_string (job->nodelist);
    }
    else if (streq (attr, "expiration")) {
        /* job->expiration potentially < 0 if R invalid */
        if (!(job->states_mask & FLUX_JOB_STATE_RUN)
            || job->expiration < 0)
            return 0;
        val = json_real (job->expiration);
    }
    else if (streq (attr, "waitstatus")) {
        if (job->wait_status < 0)
            return 0;
        val = json_integer (job->wait_status);
    }
    else if (streq (attr, "success")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_boolean (job->success);
    }
    else if (streq (attr, "exception_occurred")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_boolean (job->exception_occurred);
    }
    else if (streq (attr, "exception_severity")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred)
            return 0;
        val = json_integer (job->exception_severity);
    }
    else if (streq (attr, "exception_type")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred)
            return 0;
        val = json_string (job->exception_type);
    }
    else if (streq (attr, "exception_note")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred
            || !job->exception_note)
            return 0;
        val = json_string (job->exception_note);
    }
    else if (streq (attr, "result")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_integer (job->result);
    }
    else if (streq (attr, "annotations")) {
        if (!job->annotations)
            return 0;
        val = json_incref (job->annotations);
    }
    else if (streq (attr, "dependencies")) {
        if (!job->dependencies)
            return 0;
        if (job->dependencies_db)
            val = json_incref (job->dependencies_db);
        else
            val = json_incref (grudgeset_tojson (job->dependencies));
    }
    else {
        errprintf (errp, "%s is not a valid attribute", attr);
        errno = EINVAL;
        return -1;
    }
out:
    if (val == NULL) {
        errno = ENOMEM;
        return -1;
    }
    if (json_object_set_new (o, attr, val) < 0) {
        json_decref (val);
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

int store_all_attr (struct job *job, json_t *o, flux_error_t *errp)
{
    const char **ptr = job_attrs ();

    assert (ptr);

    while (*ptr) {
        if (store_attr (job, *ptr, o, errp) < 0)
            return -1;
        ptr++;
    }

    return 0;
}

/* For a given job, create a JSON object containing the jobid and any
 * additional requested attributes and their values.  Returns JSON
 * object which the caller must free.  On error, return NULL with
 * errno set:
 *
 * EPROTO - malformed attrs array
 * ENOMEM - out of memory
 */
json_t *job_to_json (struct job *job, json_t *attrs, flux_error_t *errp)
{
    json_t *val = NULL;
    size_t index;
    json_t *value;
    json_t *o;

    memset (errp, 0, sizeof (*errp));

    if (!(o = json_object ()))
        goto error_nomem;
    if (!(val = json_integer (job->id)))
        goto error_nomem;
    if (json_object_set_new (o, "id", val) < 0) {
        json_decref (val);
        goto error_nomem;
    }
    json_array_foreach (attrs, index, value) {
        const char *attr = json_string_value (value);
        if (!attr) {
            errprintf (errp, "attr has no string value");
            errno = EINVAL;
            goto error;
        }
        if (streq (attr, "all")) {
            if (store_all_attr (job, o, errp) < 0)
                goto error;
        }
        else {
            if (store_attr (job, attr, o, errp) < 0)
                goto error;
        }
    }
    return o;
 error_nomem:
    errno = ENOMEM;
 error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

json_t *job_to_json_dbdata (struct job *job, flux_error_t *errp)
{
    json_t *val = NULL;
    json_t *o;

    memset (errp, 0, sizeof (*errp));

    if (!(o = json_object ()))
        goto error_nomem;
    if (!(val = json_integer (job->id)))
        goto error_nomem;
    if (json_object_set_new (o, "id", val) < 0) {
        json_decref (val);
        goto error_nomem;
    }
    if (store_all_attr (job, o, errp) < 0)
        goto error;
    if (!(val = json_integer (job->states_mask)))
        goto error_nomem;
    if (json_object_set_new (o, "states_mask", val) < 0) {
        json_decref (val);
        goto error_nomem;
    }
    return o;
error_nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
