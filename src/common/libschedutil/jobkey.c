/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include <stdio.h>
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libutil/fluid.h"
#include "jobkey.h"


int schedutil_jobkey (char *buf, int bufsz, bool active,
                      flux_jobid_t id, const char *key)
{
    char idstr[32];
    int len;

    if (fluid_encode (idstr, sizeof (idstr), id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    len = snprintf (buf, bufsz, "job.%s.%s%s%s",
                    active ? "active" : "inactive",
                    idstr,
                    key ? "." : "",
                    key ? key : "");
    if (len >= bufsz)
        return -1;
    return len;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
