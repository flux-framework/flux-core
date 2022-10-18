/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <flux/core.h>

#include "util.h"

static int get_attr_uint (flux_t *h, const char *attr, unsigned int *valp)
{
    const char *s;
    unsigned long val;

    if (!(s = flux_attr_get (h, attr)))
        return -1;
    errno = 0;
    val = strtoul (s, NULL, 10);
    if (errno != 0)
        return -1;
    (*valp) = val;
    return 0;
}

int flux_get_process_scope (flux_process_scope_t *scope)
{
    flux_t *h;
    unsigned int instance_level;
    uid_t security_owner;
    const char *jobid;
    const char *kvsns;

    if (!scope) {
        errno = EINVAL;
        return -1;
    }
    if (!(h = flux_open (NULL, 0))) {
        if (errno != ENOENT)
            return -1;
        (*scope) = FLUX_PROCESS_SCOPE_NONE;
        return 0;
    }

    if (get_attr_uint (h, "instance-level", &instance_level) < 0)
        return -1;

    if (get_attr_uint (h, "security.owner", &security_owner) < 0)
        return -1;

    jobid = flux_attr_get (h, "jobid");

    if (instance_level == 0
        && !jobid
        && security_owner != getuid ()) {
        (*scope) = FLUX_PROCESS_SCOPE_SYSTEM_INSTANCE;
        return 0;
    }
    /* else
     *   instance_level > 0 - running within flux job
     *   jobid != NULL - running sub instance
     *   security_owner == getuid () - user instance
     */

    kvsns = getenv ("FLUX_KVS_NAMESPACE");
    if (kvsns)
        (*scope) = FLUX_PROCESS_SCOPE_JOB;
    else
        (*scope) = FLUX_PROCESS_SCOPE_INITIAL_PROGRAM;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
