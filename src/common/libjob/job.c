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
#include <unistd.h>
#include <sys/types.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
#include <jansson.h>

#include "job.h"
#include "sign_none.h"
#include "src/common/libutil/fluid.h"

#if HAVE_FLUX_SECURITY
/* If a textual error message is available in flux-security,
 * enclose it in a future and return.  Otherwise return NULL
 * with errno set to the original error.
 */
static flux_future_t *get_security_error (flux_security_t *sec)
{
    int errnum = flux_security_last_errnum (sec);
    const char *errmsg = flux_security_last_error (sec);
    flux_future_t *f = NULL;

    if (errmsg && (f = flux_future_create (NULL, NULL)))
        flux_future_fulfill_error (f, errnum, errmsg);
    errno = errnum;
    return f;
}

/* Cache flux-security context in the handle on first use.
 * On failure, return NULL and set the value of 'f_error' to NULL,
 * or if a textual error message is available such as from config
 * file parsing, set the value of 'f_error' to a future containing
 * the textual error.
 */
static flux_security_t *get_security_ctx (flux_t *h, flux_future_t **f_error)
{
    const char *auxkey = "flux::job_security_ctx";
    flux_security_t *sec = flux_aux_get (h, auxkey);

    if (!sec) {
        if (!(sec = flux_security_create (0)))
            goto error;
        if (flux_security_configure (sec, NULL) < 0)
            goto error;
        if (flux_aux_set (h, auxkey, sec,
                          (flux_free_f)flux_security_destroy) < 0)
            goto error;
    }
    return sec;
error:
    *f_error = sec ? get_security_error (sec) : NULL;
    flux_security_destroy (sec);
    return NULL;
}
#endif

flux_future_t *flux_job_submit (flux_t *h, const char *jobspec, int priority,
                                int flags)
{
    flux_future_t *f = NULL;
    const char *J;
    char *s = NULL;
    int saved_errno;

    if (!h || !jobspec) {
        errno = EINVAL;
        return NULL;
    }
    if (!(flags & FLUX_JOB_PRE_SIGNED)) {
#if HAVE_FLUX_SECURITY
        flux_security_t *sec;
        if (!(sec = get_security_ctx (h, &f)))
            return f;
        if (!(J = flux_sign_wrap (sec, jobspec, strlen (jobspec), NULL, 0)))
            return get_security_error (sec);
#else
        if (!(s = sign_none_wrap (jobspec, strlen (jobspec), geteuid ())))
            goto error;
        J = s;
#endif
    }
    else {
        J = jobspec;
        flags &= ~FLUX_JOB_PRE_SIGNED; // client only flag
    }
    if (!(f = flux_rpc_pack (h, "job-ingest.submit", FLUX_NODEID_ANY, 0,
                             "{s:s s:i s:i}",
                             "J", J,
                             "priority", priority,
                             "flags", flags)))
        goto error;
    return f;
error:
    saved_errno = errno;
    free (s);
    errno = saved_errno;
    return NULL;
}

int flux_job_submit_get_id (flux_future_t *f, flux_jobid_t *jobid)
{
    flux_jobid_t id;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f, "{s:I}",
                                "id", &id) < 0)
        return -1;
    if (jobid)
        *jobid = id;
    return 0;
}

flux_future_t *flux_job_list (flux_t *h, int max_entries, const char *json_str)
{
    flux_future_t *f;
    json_t *o = NULL;
    int saved_errno;

    if (!h || max_entries < 0 || !json_str
           || !(o = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-manager.list", FLUX_NODEID_ANY, 0,
                             "{s:i s:o}",
                             "max_entries", max_entries,
                             "attrs", o))) {
        saved_errno = errno;
        json_decref (o);
        errno = saved_errno;
        return NULL;
    }
    return f;
}

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

flux_future_t *flux_job_set_priority (flux_t *h, flux_jobid_t id, int priority)
{
    flux_future_t *f;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-manager.priority", FLUX_NODEID_ANY, 0,
                             "{s:I s:i}",
                             "id", id,
                             "priority", priority)))
        return NULL;
    return f;
}

static int buffer_arg_check (char *buf, int bufsz)
{
    if (!buf || bufsz <= 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_job_kvs_key (char *buf, int bufsz, flux_jobid_t id, const char *key)
{
    char idstr[32];
    int len;

    if (buffer_arg_check (buf, bufsz) < 0)
        return -1;

    if (fluid_encode (idstr, sizeof (idstr), id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    len = snprintf (buf, bufsz, "job.%s%s%s",
                    idstr,
                    key ? "." : "",
                    key ? key : "");
    if (len >= bufsz) {
        errno = EOVERFLOW;
        return -1;
    }
    return len;
}

int flux_job_kvs_guest_key (char *buf,
                            int bufsz,
                            flux_jobid_t id,
                            const char *key)
{
    char idstr[32];
    int len;

    if (buffer_arg_check (buf, bufsz) < 0)
        return -1;
    if (getenv ("FLUX_KVS_NAMESPACE"))
        len = snprintf (buf, bufsz, "%s", key ? key : ".");
    else {
        if (fluid_encode (idstr, sizeof (idstr), id, FLUID_STRING_DOTHEX) < 0)
            return -1;
        len = snprintf (buf, bufsz, "job.%s.guest%s%s",
                        idstr,
                        key ? "." : "",
                        key ? key : "");
    }
    if (len >= bufsz) {
        errno = EOVERFLOW;
        return -1;
    }
    return len;
}

int flux_job_kvs_namespace (char *buf, int bufsz, flux_jobid_t id)
{
    int len;
    if (buffer_arg_check (buf, bufsz) < 0)
        return -1;
    if ((len = snprintf (buf, bufsz, "job-%ju", id)) >= bufsz) {
        errno = EOVERFLOW;
        return -1;
    }
    return len;
}

flux_future_t *flux_job_event_watch (flux_t *h, flux_jobid_t id)
{
    flux_future_t *f;
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, rpc_flags,
                             "{s:I s:s}",
                             "id", id,
                             "path", "eventlog")))
        return NULL;
    return f;
}

int flux_job_event_watch_get (flux_future_t *f, const char **event)
{
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", "event", &s) < 0)
        return -1;
    if (event)
        *event = s;
    return 0;
}

int flux_job_event_watch_cancel (flux_future_t *f)
{
    flux_future_t *f2;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              "job-info.eventlog-watch-cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        return -1;
    flux_future_destroy (f2);
    return 0;
}

const char *flux_job_statetostr (flux_job_state_t state, bool single_char)
{
    switch (state) {
        case FLUX_JOB_NEW:
            return single_char ? "N" : "NEW";
        case FLUX_JOB_DEPEND:
            return single_char ? "D" : "DEPEND";
        case FLUX_JOB_SCHED:
            return single_char ? "S" : "SCHED";
        case FLUX_JOB_RUN:
            return single_char ? "R" : "RUN";
        case FLUX_JOB_CLEANUP:
            return single_char ? "C" : "CLEANUP";
        case FLUX_JOB_INACTIVE:
            return single_char ? "I" : "INACTIVE";
    }
    return single_char ? "?" : "(unknown)";
}

int flux_job_strtostate (const char *s, flux_job_state_t *state)
{
    if (!s || !state)
        goto inval;
    if (!strcasecmp (s, "N") || !strcasecmp (s, "NEW"))
        *state = FLUX_JOB_NEW;
    else if (!strcasecmp (s, "D") || !strcasecmp (s, "DEPEND"))
        *state = FLUX_JOB_DEPEND;
    else if (!strcasecmp (s, "S") || !strcasecmp (s, "SCHED"))
        *state = FLUX_JOB_SCHED;
    else if (!strcasecmp (s, "R") || !strcasecmp (s, "RUN"))
        *state = FLUX_JOB_RUN;
    else if (!strcasecmp (s, "C") || !strcasecmp (s, "CLEANUP"))
        *state = FLUX_JOB_CLEANUP;
    else if (!strcasecmp (s, "I") || !strcasecmp (s, "INACTIVE"))
        *state = FLUX_JOB_INACTIVE;
    else
        goto inval;

    return 0;
inval:
    errno = EINVAL;
    return -1;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
