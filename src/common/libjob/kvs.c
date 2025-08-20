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

#include "src/common/libutil/fluid.h"

#include "job.h"

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
    len = snprintf (buf,
                    bufsz,
                    "job.%s%s%s",
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
        len = snprintf (buf,
                        bufsz,
                        "job.%s.guest%s%s",
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
    if ((len = snprintf (buf, bufsz, "job-%ju", (uintmax_t)id)) >= bufsz) {
        errno = EOVERFLOW;
        return -1;
    }
    return len;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
