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
#include <stdarg.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "job_util.h"
#include "job_state.h"

void seterror (job_info_error_t *errp, const char *fmt, ...)
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

/* For a given job, create a JSON object containing the jobid and any
 * additional requested attributes and their values.  Returns JSON
 * object which the caller must free.  On error, return NULL with
 * errno set:
 *
 * EPROTO - malformed attrs array
 * ENOMEM - out of memory
 */
json_t *job_to_json (struct job *job, json_t *attrs, job_info_error_t *errp)
{
    size_t index;
    json_t *value;
    json_t *o;
    json_t *val = NULL;

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
        if (!strcmp (attr, "userid")) {
            val = json_integer (job->userid);
        }
        else if (!strcmp (attr, "urgency")) {
            val = json_integer (job->urgency);
        }
        else if (!strcmp (attr, "t_submit")
                 || !strcmp (attr, "t_depend")) {
            if (!(job->states_mask & FLUX_JOB_STATE_DEPEND))
                continue;
            val = json_real (job->t_submit);
        }
        else if (!strcmp (attr, "t_run")) {
            if (!(job->states_mask & FLUX_JOB_STATE_RUN))
                continue;
            val = json_real (job->t_run);
        }
        else if (!strcmp (attr, "t_cleanup")) {
            if (!(job->states_mask & FLUX_JOB_STATE_CLEANUP))
                continue;
            val = json_real (job->t_cleanup);
        }
        else if (!strcmp (attr, "t_inactive")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
                continue;
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
                continue;
            val = json_integer (job->nnodes);
        }
        else if (!strcmp (attr, "ranks")) {
            if (!(job->states_mask & FLUX_JOB_STATE_RUN))
                continue;
            /* potentially NULL if R invalid */
            if (job->ranks)
                val = json_string (job->ranks);
            else
                val = json_string ("");
        }
        else if (!strcmp (attr, "nodelist")) {
            if (!(job->states_mask & FLUX_JOB_STATE_RUN))
                continue;
            /* potentially NULL if R invalid */
            if (job->nodelist)
                val = json_string (job->nodelist);
            else
                val = json_string ("");
        }
        else if (!strcmp (attr, "expiration")) {
            if (!(job->states_mask & FLUX_JOB_STATE_RUN))
                continue;
            val = json_real (job->expiration);
        }
        else if (!strcmp (attr, "success")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
                continue;
            val = json_boolean (job->success);
        }
        else if (!strcmp (attr, "exception_occurred")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
                continue;
            val = json_boolean (job->exception_occurred);
        }
        else if (!strcmp (attr, "exception_severity")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_integer (job->exception_severity);
        }
        else if (!strcmp (attr, "exception_type")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_string (job->exception_type);
        }
        else if (!strcmp (attr, "exception_note")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE)
                || !job->exception_occurred)
                continue;
            val = json_string (job->exception_note);
        }
        else if (!strcmp (attr, "result")) {
            if (!(job->states_mask & FLUX_JOB_STATE_INACTIVE))
                continue;
            val = json_integer (job->result);
        }
        else if (!strcmp (attr, "annotations")) {
            if (!job->annotations)
                continue;
            val = json_incref (job->annotations);
        }
        else {
            seterror (errp, "%s is not a valid attribute", attr);
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
