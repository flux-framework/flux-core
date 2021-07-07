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

#include "job.h"

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
