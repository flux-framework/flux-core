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
#include "strtab.h"

static struct strtab states[] = {
    { FLUX_JOB_STATE_NEW,       "NEW",      "new",      "N", "n" },
    { FLUX_JOB_STATE_DEPEND,    "DEPEND",   "depend",   "D", "d" },
    { FLUX_JOB_STATE_PRIORITY,  "PRIORITY", "priority", "P", "p" },
    { FLUX_JOB_STATE_SCHED,     "SCHED",    "sched",    "S", "s" },
    { FLUX_JOB_STATE_RUN,       "RUN",      "run",      "R", "r" },
    { FLUX_JOB_STATE_CLEANUP,   "CLEANUP",  "cleanup",  "C", "c" },
    { FLUX_JOB_STATE_INACTIVE,  "INACTIVE", "inactive", "I", "i" },
};
static const size_t states_count = sizeof (states) / sizeof (states[0]);


const char *flux_job_statetostr (flux_job_state_t state, const char *fmt)
{
    return strtab_numtostr (state, fmt, states, states_count);
}

int flux_job_strtostate (const char *s, flux_job_state_t *state)
{
    int num;

    if ((num = strtab_strtonum (s, states, states_count)) < 0)
        return -1;
    if (state)
        *state = num;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
