/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.  Additionally, the libflux-core library may be
 *  redistributed under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

    if (!f || !jobid) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f, "{s:I}",
                                "id", &id) < 0)
        return -1;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
