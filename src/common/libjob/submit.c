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
//#include <ctype.h>
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif
//#include <jansson.h>

#include "job.h"
#include "sign_none.h"
//#include "src/common/libutil/fluid.h"
//#include "src/common/libeventlog/eventlog.h"

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
        if (flux_aux_set (h,
                          auxkey,
                          sec,
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

flux_future_t *flux_job_submit (flux_t *h,
                                const char *jobspec,
                                int urgency,
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
        const char *owner;

        /* Security note:
         * Instance owner jobs do not need a cryptographic signature since
         * they do not require the IMP to be executed.  Force the signing
         * mechanism to 'none' if the broker security.owner matches getuid ().
         * This side-steps the requirement that the munge daemon is running
         * for single user instances compiled --with-flux-security, as
         * described in flux-framework/flux-core#3305.
         *
         * This method also works with flux-proxy(1) as described in
         * flux-framework/flux-core#5530.
         *
         * N.B. Guest submissions signed with mech=none are summarily rejected
         * by job-ingest so the impact of getting this code wrong is job
         * submission failure, not any weakening of security.
         */
        if ((owner = flux_attr_get (h, "security.owner"))) {
            errno = 0;
            unsigned int userid = strtoul (owner, NULL, 10);
            if (errno == 0 && userid == getuid ())
                mech = "none";
        }
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
    if (!(f = flux_rpc_pack (h,
                             "job-ingest.submit",
                             FLUX_NODEID_ANY,
                             0,
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
    if (flux_rpc_get_unpack (f, "{s:I}", "id", &id) < 0)
        return -1;
    if (jobid)
        *jobid = id;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
