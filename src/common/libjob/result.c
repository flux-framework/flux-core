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
#include <assert.h>
#include <flux/core.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#include "job.h"
#include "strtab.h"

#include "src/common/libeventlog/eventlog.h"

static struct strtab results[] = {
    { FLUX_JOB_RESULT_COMPLETED, "COMPLETED", "completed", "CD", "cd" },
    { FLUX_JOB_RESULT_FAILED,    "FAILED",    "failed",    "F",  "f" },
    { FLUX_JOB_RESULT_CANCELED,  "CANCELED",  "canceled",  "CA", "ca" },
    { FLUX_JOB_RESULT_TIMEOUT,   "TIMEOUT",   "timeout",   "TO", "to" },
};


const char *flux_job_resulttostr (flux_job_result_t result, const char *fmt)
{
    return strtab_numtostr (result, fmt, results, ARRAY_SIZE (results));
}

int flux_job_strtoresult (const char *s, flux_job_result_t *result)
{
    int num;

    if ((num = strtab_strtonum (s, results, ARRAY_SIZE (results))) < 0)
        return -1;
    if (result)
        *result = num;
    return 0;
}

static flux_job_result_t job_result_calc (json_t *res)
{
    double t_run = -1.;
    int status = -1;
    bool exception_occurred = false;
    const char *exception_type = NULL;
    json_error_t error;

    if (json_unpack_ex (res,
                        &error,
                        0,
                        "{s?f s:b s?i s?s}",
                        "t_run", &t_run,
                        "exception_occurred", &exception_occurred,
                        "waitstatus", &status,
                        "exception_type", &exception_type) < 0)
        return FLUX_JOB_RESULT_FAILED;

    if (t_run > 0. && status == 0)
        return FLUX_JOB_RESULT_COMPLETED;
    if (exception_occurred) {
        if (exception_type != NULL) {
            if (streq (exception_type, "cancel"))
                return FLUX_JOB_RESULT_CANCELED;
            if (streq (exception_type, "timeout"))
                return FLUX_JOB_RESULT_TIMEOUT;
        }
    }
    return FLUX_JOB_RESULT_FAILED;
}

static void result_eventlog_error_cb (flux_future_t *f, void *arg)
{
    json_t *res = arg;
    json_t *o = NULL;
    char *s = NULL;

    if (flux_future_get (f, NULL) < 0 && errno != ENODATA) {
        flux_future_continue_error (f, errno, NULL);
        goto out;
    }
    flux_job_result_t result = job_result_calc (res);
    if (!(o = json_integer (result))
        || json_object_set_new (res, "result", o) < 0
        || !(s = json_dumps (res, JSON_COMPACT))) {
        flux_future_continue_error (f, ENOMEM, NULL);
        goto out;
    }
    flux_future_fulfill_next (f, s, free);
out:
    flux_future_destroy (f);
}


static int result_exception_severity (json_t *res)
{
    json_t *sev = json_object_get (res, "exception_severity");
    if (sev) {
        return json_integer_value (sev);
    }
    return -1;
}

static int job_result_handle_exception (json_t *res,
                                        json_t *context)
{
    json_t *type;
    json_t *severity;
    json_t *note;

    if (json_unpack (context,
                     "{s:o s:o s:o}",
                     "type", &type,
                     "severity", &severity,
                     "note", &note) < 0) {
        errno = EPROTO;
        return -1;
    }

    if (json_is_true (json_object_get (res, "exception_occurred"))) {
        /* Only overwrite previous exception if the latest
         *  is of greater severity.
         */
        int sev = json_integer_value (severity);
        int prev_sev = result_exception_severity (res);
        if (prev_sev > 0 && prev_sev < sev)
            return 0;
    }
    if (json_object_set (res, "exception_occurred", json_true ()) < 0
        || json_object_set (res, "exception_type", type) < 0
        || json_object_set (res, "exception_note", note) < 0
        || json_object_set (res, "exception_severity", severity) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static void result_eventlog_cb (flux_future_t *f, void *arg)
{
    json_t *res = arg;
    const char *entry = NULL;
    const char *name = NULL;
    json_t *o = NULL;
    json_t *context = NULL;
    json_t *timestamp = NULL;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        /* This should never happen, since this is an "and_then" callback
         */
        goto error;
    }
    if (!(o = eventlog_entry_decode (entry)))
        goto error;
    if (!(timestamp = json_object_get (o, "timestamp"))) {
        errno = EPROTO;
        goto error;
    }
    if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
        goto error;

    if (streq (name, "submit")) {
        if (json_object_set (res, "t_submit", timestamp) < 0)
            goto enomem;
    }
    else if (streq (name, "alloc")) {
        if (json_object_set (res, "t_run", timestamp) < 0)
            goto enomem;
    }
    else if (streq (name, "finish")) {
        json_t *wstatus = NULL;
        if (json_object_set (res, "t_cleanup", timestamp) < 0)
            goto enomem;
        if (!(wstatus = json_object_get (context, "status"))) {
            errno = EPROTO;
            goto error;
        }
        if (json_object_set (res, "waitstatus", wstatus) < 0)
            goto enomem;
    }
    else if (streq (name, "exception")) {
        if (job_result_handle_exception (res, context) < 0)
            goto error;
    }

    json_decref (o);

    /*  Ensure "next" future is not auto-continued by chained future
     *   implementation. This is non-obvious, but if this call is not
     *   made then our next future would be prematurely fulfilled.
     */
    flux_future_continue (f, NULL);
    flux_future_reset (f);
    return;
enomem:
    errno = ENOMEM;
error:
    flux_future_continue_error (f, errno, NULL);
    flux_future_destroy (f);
}

int flux_job_result_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    json_t *res;
    json_error_t error;
    int rc;
    va_list ap;

    if (f == NULL
        || !(res = flux_future_aux_get (f, "flux::result"))) {
        errno = EINVAL;
        return -1;
    }
    if (flux_future_get (f, NULL) < 0)
        return -1;

    va_start (ap, fmt);
    rc = json_vunpack_ex (res, &error, 0, fmt, ap);
    va_end (ap);
    if (rc < 0)
        errno = EINVAL;
    return rc;
}

int flux_job_result_get (flux_future_t *f,
                         const char **json_str)
{
    return flux_future_get (f, (const void **) json_str);
}

json_t *job_result_alloc (flux_jobid_t id)
{
    return json_pack ("{s:I s:b}",
                      "id", id,
                      "exception_occurred", false);
}

flux_future_t *flux_job_result (flux_t *h, flux_jobid_t id, int flags)
{
    json_t *res = NULL;
    flux_future_t *f = NULL;
    flux_future_t *event_f = NULL;
    int saved_errno;

    if (!(res = job_result_alloc (id))
        || !(event_f = flux_job_event_watch (h, id, "eventlog", 0))
        || !(f = flux_future_and_then (event_f, result_eventlog_cb, res))
        || !(f = flux_future_or_then (event_f,
                                      result_eventlog_error_cb,
                                      res))
        || flux_future_aux_set (f,
                                "flux::result",
                                res,
                                (flux_free_f) json_decref) < 0)
        goto error;

    return f;
error:
    saved_errno = errno;
    json_decref (res);
    if (event_f) {
        flux_job_event_watch_cancel (event_f);
        flux_future_destroy (f);
    }
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
