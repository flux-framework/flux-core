/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <math.h>

#include "src/common/libutil/errprintf.h"

#include "job.h"

flux_future_t *flux_job_raise (flux_t *h, flux_jobid_t id,
                               const char *type, int severity, const char *note)
{
    flux_future_t *f;
    json_t *o;
    int saved_errno;

    if (!h || !type) {
        errno = EINVAL;
        return NULL;
    }
    if (!(o = json_pack ("{s:I s:s s:i}",
                         "id", id,
                         "type", type,
                         "severity", severity)))
        goto nomem;
    if (note) {
        json_t *o_note = json_string (note);
        if (!o_note || json_object_set_new (o, "note", o_note) < 0) {
            json_decref (o_note);
            goto nomem;
        }
    }
    if (!(f = flux_rpc_pack (h, "job-manager.raise", FLUX_NODEID_ANY, 0,
                                                                    "o", o)))
        goto error;
    return f;
nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return NULL;
}

flux_future_t *flux_job_cancel (flux_t *h, flux_jobid_t id, const char *reason)
{
    return flux_job_raise (h, id, "cancel", 0, reason);
}

flux_future_t *flux_job_kill (flux_t *h, flux_jobid_t id, int signum)
{
    if (!h || signum <= 0) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h, "job-manager.kill", FLUX_NODEID_ANY, 0,
                             "{s:I s:i}",
                             "id", id,
                             "signum", signum);
}

flux_future_t *flux_job_set_urgency (flux_t *h, flux_jobid_t id, int urgency)
{
    flux_future_t *f;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-manager.urgency", FLUX_NODEID_ANY, 0,
                             "{s:I s:i}",
                             "id", id,
                             "urgency", urgency)))
        return NULL;
    return f;
}

int flux_job_timeleft (flux_t *h, flux_error_t *errp, double *timeleft)
{
    flux_jobid_t id;
    flux_job_state_t state;
    const char *s;
    double expiration = 0.;
    flux_future_t *f = NULL;
    flux_t *parent_h = NULL;
    int rc = -1;

    if (!h || !timeleft) {
        errno = EINVAL;
        return errprintf (errp, "Invalid argument");
    }

    /*  Check for FLUX_JOB_ID environment variable. If set, this process
     *  is part of a job in the current instance. If not, then check to
     *  see if this process is part of an "initial program".
     */
    if (!(s = getenv ("FLUX_JOB_ID"))) {
        const char *uri;

        /*  Check if we're in "initial program" context.
         *  If not, then this instance may be the system instance, or
         *  may be a job in a foreign RM. Either way we cannot provide
         *  a remaining time, so return an error.
         */
        if (!(s = flux_attr_get (h, "jobid"))) {
            errprintf (errp,
                       "unable to associate this process with a Flux jobid");
            return -1;
        }

        /* This is an initial program. Switch the handle to the parent */
        if (!(uri = flux_attr_get (h, "parent-uri")))
            return errprintf (errp,
                              "failed to get parent-uri attribute: %s",
                              strerror (errno));
        if (!(parent_h = flux_open (uri, 0)))
            return errprintf (errp,
                              "failed to connect to parent instance: %s",
                              strerror (errno));
        h = parent_h;
    }

    /*  Parse jobid and lookup expiration
     */
    if (flux_job_id_parse (s, &id) < 0) {
        errprintf (errp, "failed to parse jobid %s", s);
        goto out;
    }

    /*  Fetch job expiration from this or parent's job-list service
     */
    if (!(f = flux_job_list_id (h, id, "[\"expiration\", \"state\"]"))) {
        errprintf (errp, "flux_job_list_id: %s: %s", s, strerror (errno));
        goto out;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:{s?f s:i}}",
                             "job",
                             "expiration", &expiration,
                             "state", &state) < 0) {
        if (errno == ENOENT)
            errprintf (errp, "%s: no such jobid", s);
        else
            errprintf (errp,
                       "flux_job_list_id: %s: %s",
                       s,
                       future_strerror (f, errno));
        goto out;
    }
    if (state & FLUX_JOB_STATE_PENDING) {
        /*  The remaining time for a pending job is undefined, so report
         *  an error instead.
         */
        errprintf (errp, "job %s has not started", s);
        goto out;
    }
    else if (state != FLUX_JOB_STATE_RUN) {
        /*  Only jobs in RUN state have any time left.
         *  Return 0 for jobs in any other state besides RUN.
         */
        *timeleft = 0.;
    }
    else if (expiration == 0.) {
        /*  If expiration is 0. then job time left is unlimited.
         *  Return INFINITY.
         */
        *timeleft = INFINITY;
    }
    else {
        *timeleft = expiration - flux_reactor_now (flux_get_reactor (h));
        /*  Avoid returning negative number. If expiration has elapsed,
         *  then the time remaining is 0.
         */
        if (*timeleft < 0.)
            *timeleft = 0.;
    }
    rc = 0;
out:
    flux_future_destroy (f);
    flux_close (parent_h);
    return rc;
}

int flux_job_waitstatus_to_exitcode (int waitstatus, flux_error_t *errp)
{
    int code;

    /*  If waitstatus indicates WIFSIGNALED, then this means the job shell
     *  was signaled, not the tasks. Report accordingly:
     */
    if (WIFSIGNALED (waitstatus)) {
        /*  Whether the job shell or one or more tasks is terminated by a
         *  signal, set the exit code to signal + 128
         */
        code = WTERMSIG (waitstatus) + 128;
        errprintf (errp, "job shell %s", strsignal (WTERMSIG (waitstatus)));
    }
    else if (WIFEXITED (waitstatus)) {
        code = WEXITSTATUS (waitstatus);
        /*  If exit code > 128, then tasks were likely terminated by a
         *  signal. (job shell returns 128+signo in this case)
         */
        if (code > 128)
            errprintf (errp, "task(s) %s", strsignal (code - 128));
        else if (code > 0)
            errprintf (errp, "task(s) exited with exit code %d", code);
        else /* Ensure errp->text is cleared */
            err_init (errp);
    }
    else {
        errprintf (errp, "unexpected wait(2) status %d", waitstatus);
        code = -1;
        errno = EINVAL;
    }
    return code;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
