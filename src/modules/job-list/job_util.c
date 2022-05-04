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

#include "src/common/libutil/errno_safe.h"

#include "job_util.h"
#include "job_state.h"

void seterror (job_list_error_t *errp, const char *fmt, ...)
{
    if (errp) {
        va_list ap;
        int saved_errno = errno;
        va_start (ap, fmt);
        (void) vsnprintf (errp->text, sizeof (errp->text), fmt, ap);
        va_end (ap);
        errno = saved_errno;
    }
}

static int store_attr (struct job *job,
                       const char *attr,
                       json_t *o,
                       job_list_error_t *errp)
{
    json_t *val = NULL;

    if (!strcmp (attr, "userid")) {
        val = json_integer (job->userid);
    }
    else if (!strcmp (attr, "urgency")) {
        val = json_integer (job->urgency);
    }
    else if (!strcmp (attr, "priority")) {
        if (!(job->states_mask & FLUX_JOB_STATE_SCHED))
            return 0;
        val = json_integer (job->priority);
    }
    else if (!strcmp (attr, "t_submit")
             || !strcmp (attr, "t_depend")) {
        if (!(job->states_mask & FLUX_JOB_STATE_DEPEND))
            return 0;
        val = json_real (job->t_submit);
    }
    else if (!strcmp (attr, "t_run")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        val = json_real (job->t_run);
    }
    else if (!strcmp (attr, "t_cleanup")) {
        if (!(job->states_mask & FLUX_JOB_STATE_CLEANUP))
            return 0;
        val = json_real (job->t_cleanup);
    }
    else if (!strcmp (attr, "t_inactive")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_real (job->t_inactive);
    }
    else if (!strcmp (attr, "state")) {
        val = json_integer (job->state);
    }
    else if (!strcmp (attr, "name")) {
        /* potentially NULL if jobspec invalid */
        if (job->name)
            val = json_string (job->name);
        else
            val = json_string ("");
    }
    else if (!strcmp (attr, "ntasks")) {
        val = json_integer (job->ntasks);
    }
    else if (!strcmp (attr, "nnodes")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        val = json_integer (job->nnodes);
    }
    else if (!strcmp (attr, "ranks")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        /* potentially NULL if R invalid */
        if (job->ranks)
            val = json_string (job->ranks);
        else
            val = json_string ("");
    }
    else if (!strcmp (attr, "nodelist")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        /* potentially NULL if R invalid */
        if (job->nodelist)
            val = json_string (job->nodelist);
        else
            val = json_string ("");
    }
    else if (!strcmp (attr, "expiration")) {
        if (!(job->states_mask & FLUX_JOB_STATE_RUN))
            return 0;
        val = json_real (job->expiration);
    }
    else if (!strcmp (attr, "waitstatus")) {
        if (job->wait_status < 0)
            return 0;
        val = json_integer (job->wait_status);
    }
    else if (!strcmp (attr, "success")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_boolean (job->success);
    }
    else if (!strcmp (attr, "exception_occurred")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_boolean (job->exception_occurred);
    }
    else if (!strcmp (attr, "exception_severity")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred)
            return 0;
        val = json_integer (job->exception_severity);
    }
    else if (!strcmp (attr, "exception_type")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred)
            return 0;
        val = json_string (job->exception_type);
    }
    else if (!strcmp (attr, "exception_note")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
            || !job->exception_occurred)
            return 0;
        val = json_string (job->exception_note);
    }
    else if (!strcmp (attr, "result")) {
        if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
            return 0;
        val = json_integer (job->result);
    }
    else if (!strcmp (attr, "annotations")) {
        if (!job->annotations)
            return 0;
        val = json_incref (job->annotations);
    }
    else if (!strcmp (attr, "dependencies")) {
        if (!job->dependencies)
            return 0;
        val = json_incref (grudgeset_tojson (job->dependencies));
    }
    else {
        seterror (errp, "%s is not a valid attribute", attr);
        errno = EINVAL;
        return -1;
    }
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

/* For a given job, create a JSON object containing the jobid and any
 * additional requested attributes and their values.  Returns JSON
 * object which the caller must free.  On error, return NULL with
 * errno set:
 *
 * EPROTO - malformed attrs array
 * ENOMEM - out of memory
 */
json_t *job_to_json (struct job *job, json_t *attrs, job_list_error_t *errp)
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
            seterror (errp, "attr has no string value");
            errno = EINVAL;
            goto error;
        }
        if (store_attr (job, attr, o, errp) < 0)
            goto error;
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
