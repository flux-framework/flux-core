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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "job_util.h"
#include "job_state.h"

/* For a given job, create a JSON object containing the jobid and any
 * additional requested attributes and their values.  Returns JSON
 * object which the caller must free.  On error, return NULL with
 * errno set:
 *
 * EPROTO - malformed attrs array
 * ENOMEM - out of memory
 */
json_t *job_to_json (struct job *job, json_t *attrs)
{
    size_t index;
    json_t *value;
    json_t *o;
    json_t *val = NULL;

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
            errno = EINVAL;
            goto error;
        }
        if (!strcmp (attr, "userid")) {
            val = json_integer (job->userid);
        }
        else if (!strcmp (attr, "priority")) {
            val = json_integer (job->priority);
        }
        else if (!strcmp (attr, "t_submit")
                 || !strcmp (attr, "t_depend")) {
            if (!(job->states_mask & FLUX_JOB_DEPEND))
                continue;
            val = json_real (job->t_submit);
        }
        else if (!strcmp (attr, "t_sched")) {
            if (!(job->states_mask & FLUX_JOB_SCHED))
                continue;
            val = json_real (job->t_sched);
        }
        else if (!strcmp (attr, "t_run")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_real (job->t_run);
        }
        else if (!strcmp (attr, "t_cleanup")) {
            if (!(job->states_mask & FLUX_JOB_CLEANUP))
                continue;
            val = json_real (job->t_cleanup);
        }
        else if (!strcmp (attr, "t_inactive")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE))
                continue;
            val = json_real (job->t_inactive);
        }
        else if (!strcmp (attr, "state")) {
            val = json_integer (job->state);
        }
        else if (!strcmp (attr, "name")) {
            val = json_string (job->name);
        }
        else if (!strcmp (attr, "ntasks")) {
            val = json_integer (job->ntasks);
        }
        else if (!strcmp (attr, "nnodes")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_integer (job->nnodes);
        }
        else if (!strcmp (attr, "ranks")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_string (job->ranks);
        }
        else if (!strcmp (attr, "expiration")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_real (job->expiration);
        }
        else if (!strcmp (attr, "success")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE))
                continue;
            val = json_boolean (job->success);
        }
        else if (!strcmp (attr, "exception_occurred")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE))
                continue;
            val = json_boolean (job->exception_occurred);
        }
        else if (!strcmp (attr, "exception_severity")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_integer (job->exception_severity);
        }
        else if (!strcmp (attr, "exception_type")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_string (job->exception_type);
        }
        else if (!strcmp (attr, "exception_note")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_string (job->exception_note);
        }
        else if (!strcmp (attr, "result")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE))
                continue;
            val = json_integer (job->result);
        }
        else {
            errno = EINVAL;
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
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
