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
#include <ctype.h>
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

static int attr_get_u32 (flux_t *h, const char *name, uint32_t *val)
{
    const char *s;
    uint32_t v;

    if (!(s = flux_attr_get (h, name)))
        return -1;
    errno = 0;
    v = strtoul (s, NULL, 10);
    if (errno != 0)
        return -1;
    *val = v;
    return 0;
}

#endif

flux_future_t *flux_job_submit (flux_t *h, const char *jobspec, int urgency,
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
        const char *mech = NULL;
        uint32_t owner;

        /* Security note:
         * Instance owner jobs do not need a cryptographic signature since
         * they do not require the IMP to be executed.  Force the signing
         * mechanism to 'none' if the 'security.owner' broker attribute
         * == getuid() to side-step the requirement that the munge daemon
         * is running for single user instances compiled --with-flux-security,
         * as described in flux-framework/flux-core#3305.
         */
        if (attr_get_u32 (h, "security.owner", &owner) == 0
                && getuid () == owner)
            mech = "none";
        if (!(sec = get_security_ctx (h, &f)))
            return f;
        if (!(J = flux_sign_wrap (sec, jobspec, strlen (jobspec), mech, 0)))
            return get_security_error (sec);
#else
        if (!(s = sign_none_wrap (jobspec, strlen (jobspec), getuid ())))
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
                             "urgency", urgency,
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

flux_future_t *flux_job_wait (flux_t *h, flux_jobid_t id)
{
    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "job-manager.wait",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:I}",
                          "id",
                          id);
}

int flux_job_wait_get_status (flux_future_t *f,
                              bool *successp,
                              const char **errstrp)
{
    int success;
    const char *errstr;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:b s:s}",
                             "success",
                             &success,
                             "errstr",
                             &errstr) < 0)
        return -1;
    if (successp)
        *successp = success ? true : false;
    if (errstrp)
        *errstrp = errstr;
    return 0;
}

int flux_job_wait_get_id (flux_future_t *f, flux_jobid_t *jobid)
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

flux_future_t *flux_job_list (flux_t *h,
                              int max_entries,
                              const char *json_str,
                              uint32_t userid,
                              int states)
{
    flux_future_t *f;
    json_t *o = NULL;
    int valid_states = (FLUX_JOB_STATE_PENDING
                        | FLUX_JOB_STATE_RUNNING
                        | FLUX_JOB_STATE_INACTIVE);
    int saved_errno;

    if (!h || max_entries < 0 || !json_str
           || !(o = json_loads (json_str, 0, NULL))
           || states & ~valid_states) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-list.list", FLUX_NODEID_ANY, 0,
                             "{s:i s:o s:i s:i s:i}",
                             "max_entries", max_entries,
                             "attrs", o,
                             "userid", userid,
                             "states", states,
                             "results", 0))) {
        saved_errno = errno;
        json_decref (o);
        errno = saved_errno;
        return NULL;
    }
    return f;
}

flux_future_t *flux_job_list_inactive (flux_t *h,
                                       int max_entries,
                                       double since,
                                       const char *json_str)
{
    flux_future_t *f;
    json_t *o = NULL;
    int saved_errno;

    if (!h || max_entries < 0 || since < 0. || !json_str
           || !(o = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-list.list-inactive", FLUX_NODEID_ANY, 0,
                             "{s:i s:f s:o}",
                             "max_entries", max_entries,
                             "since", since,
                             "attrs", o))) {
        saved_errno = errno;
        json_decref (o);
        errno = saved_errno;
        return NULL;
    }
    return f;
}

flux_future_t *flux_job_list_id (flux_t *h,
                                 flux_jobid_t id,
                                 const char *json_str)
{
    flux_future_t *f;
    json_t *o = NULL;
    int saved_errno;

    if (!h || !json_str
           || !(o = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-list.list-id", FLUX_NODEID_ANY, 0,
                             "{s:I s:O}",
                             "id", id,
                             "attrs", o)))
        goto error;

    json_decref (o);
    return f;

error:
    saved_errno = errno;
    json_decref (o);
    errno = saved_errno;
    return NULL;
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

flux_future_t *flux_job_event_watch (flux_t *h, flux_jobid_t id,
                                     const char *path, int flags)
{
    flux_future_t *f;
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    bool guest = false;

    /* No flags supported yet */
    if (!h || !path || flags) {
        errno = EINVAL;
        return NULL;
    }
    if (path && !strncmp (path, "guest.", 6)) {
        topic = "job-info.guest-eventlog-watch";
        path += 6;
        guest = true;
    }
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, rpc_flags,
                             "{s:I s:s s:i}",
                             "id", id,
                             "path", path,
                             "flags", flags)))
        return NULL;
    if (guest) {
        /* value not relevant, set to anything */
        if (flux_future_aux_set (f, "guest", "", NULL) < 0) {
            flux_future_destroy (f);
            return NULL;
        }
    }
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
    const char *topic = "job-info.eventlog-watch-cancel";

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_future_aux_get (f, "guest") != NULL)
        topic = "job-info.guest-eventlog-watch-cancel";
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              topic,
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
        case FLUX_JOB_STATE_NEW:
            return single_char ? "N" : "NEW";
        case FLUX_JOB_STATE_DEPEND:
            return single_char ? "D" : "DEPEND";
        case FLUX_JOB_STATE_PRIORITY:
            return single_char ? "P" : "PRIORITY";
        case FLUX_JOB_STATE_SCHED:
            return single_char ? "S" : "SCHED";
        case FLUX_JOB_STATE_RUN:
            return single_char ? "R" : "RUN";
        case FLUX_JOB_STATE_CLEANUP:
            return single_char ? "C" : "CLEANUP";
        case FLUX_JOB_STATE_INACTIVE:
            return single_char ? "I" : "INACTIVE";
    }
    return single_char ? "?" : "(unknown)";
}

int flux_job_strtostate (const char *s, flux_job_state_t *state)
{
    if (!s || !state)
        goto inval;
    if (!strcasecmp (s, "N") || !strcasecmp (s, "NEW"))
        *state = FLUX_JOB_STATE_NEW;
    else if (!strcasecmp (s, "D") || !strcasecmp (s, "DEPEND"))
        *state = FLUX_JOB_STATE_DEPEND;
    else if (!strcasecmp (s, "P") || !strcasecmp (s, "PRIORITY"))
        *state = FLUX_JOB_STATE_PRIORITY;
    else if (!strcasecmp (s, "S") || !strcasecmp (s, "SCHED"))
        *state = FLUX_JOB_STATE_SCHED;
    else if (!strcasecmp (s, "R") || !strcasecmp (s, "RUN"))
        *state = FLUX_JOB_STATE_RUN;
    else if (!strcasecmp (s, "C") || !strcasecmp (s, "CLEANUP"))
        *state = FLUX_JOB_STATE_CLEANUP;
    else if (!strcasecmp (s, "I") || !strcasecmp (s, "INACTIVE"))
        *state = FLUX_JOB_STATE_INACTIVE;
    else
        goto inval;

    return 0;
inval:
    errno = EINVAL;
    return -1;
}

const char *flux_job_resulttostr (flux_job_result_t result, bool abbrev)
{
    switch (result) {
        case FLUX_JOB_RESULT_COMPLETED:
            return abbrev ? "CD" : "COMPLETED";
        case FLUX_JOB_RESULT_FAILED:
            return abbrev ? "F" : "FAILED";
        case FLUX_JOB_RESULT_CANCELED:
            return abbrev ? "CA" : "CANCELED";
        case FLUX_JOB_RESULT_TIMEOUT:
            return abbrev ? "TO" : "TIMEOUT";
    }
    return abbrev ? "?" : "(unknown)";
}

int flux_job_strtoresult (const char *s, flux_job_result_t *result)
{
    if (!s || !result)
        goto inval;
    if (!strcasecmp (s, "CD") || !strcasecmp (s, "COMPLETED"))
        *result = FLUX_JOB_RESULT_COMPLETED;
    else if (!strcasecmp (s, "F") || !strcasecmp (s, "FAILED"))
        *result = FLUX_JOB_RESULT_FAILED;
    else if (!strcasecmp (s, "CA") || !strcasecmp (s, "CANCELED"))
        *result = FLUX_JOB_RESULT_CANCELED;
    else if (!strcasecmp (s, "TO") || !strcasecmp (s, "TIMEOUT"))
        *result = FLUX_JOB_RESULT_TIMEOUT;
    else
        goto inval;

    return 0;
inval:
    errno = EINVAL;
    return -1;
}

int flux_job_id_parse (const char *s, flux_jobid_t *idp)
{
    int len;
    const char *p = s;
    if (s == NULL
        || idp == NULL
        || (len = strlen (s)) == 0) {
        errno = EINVAL;
        return -1;
    }
    /*  Remove leading whitespace
     */
    while (isspace(*p))
        p++;
    /*  Ignore any `job.` prefix. This allows a "kvs" encoding
     *   created by flux_job_id_encode(3) to properly decode.
     */
    if (strncmp (p, "job.", 4) == 0)
        p += 4;
    return fluid_parse (p, idp);
}

int flux_job_id_encode (flux_jobid_t id,
                        const char *type,
                        char *buf,
                        size_t bufsz)
{
    fluid_string_type_t t;
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (type == NULL || strcasecmp (type, "dec") == 0) {
        int len = snprintf (buf, bufsz, "%ju", (uintmax_t) id);
        if (len >= bufsz) {
            errno = ENOSPC;
            return -1;
        }
        return 0;
    }
    if (strcasecmp (type, "hex") == 0) {
        int len = snprintf (buf, bufsz, "0x%jx", (uintmax_t) id);
        if (len >= bufsz) {
            errno = ENOSPC;
            return -1;
        }
        return 0;
    }

    /* The following encodings all use fluid_encode(3).
     */
    if (strcasecmp (type, "kvs") == 0) {
        /* kvs: prepend "job." to "dothex" encoding.
         */
        int len = snprintf (buf, bufsz, "job.");
        if (len >= bufsz) {
            errno = ENOSPC;
            return -1;
        }
        buf += len;
        bufsz -= len;
        type = "dothex";
    }
    if (strcasecmp (type, "dothex") == 0)
        t = FLUID_STRING_DOTHEX;
    else if (strcasecmp (type, "words") == 0)
        t = FLUID_STRING_MNEMONIC;
    else if (strcasecmp (type, "f58") == 0)
        t = FLUID_STRING_F58;
    else {
        /*  Return EPROTO for invalid type to differentiate from
         *   other invalid arguments.
         */
        errno = EPROTO;
        return -1;
    }
    return fluid_encode (buf, bufsz, id, t);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
