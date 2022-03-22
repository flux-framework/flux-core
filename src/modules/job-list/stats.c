/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <ctype.h>
#include <flux/core.h>

#include "job_state.h"
#include "stats.h"

/*  Return the index into stats->state_count[] array for the
 *   job state 'state'
 */
static inline int state_index (flux_job_state_t state)
{
    int i = 0;
    while (!(state & (1<<i)))
        i++;
    assert (i < FLUX_JOB_NR_STATES);
    return i;
}

/*  Return a lowercase state name for the state at 'index' the
 *   stats->state_count[] array.
 */
static const char *state_index_name (int index)
{
    static char name[64];
    char *p;

    memset (name, 0, sizeof (name));
    strncpy (name,
            flux_job_statetostr ((1<<index), false),
            sizeof (name) - 1);

    for (p = name; *p != '\0'; ++p)
        *p = tolower(*p);
    return name;
}

void job_stats_update (struct job_stats *stats,
                       struct job *job,
                       flux_job_state_t newstate)
{
    stats->state_count[state_index(newstate)]++;

    /*  Stats for NEW are not tracked */
    if (job->state != FLUX_JOB_STATE_NEW)
        stats->state_count[state_index(job->state)]--;

    if (newstate == FLUX_JOB_STATE_INACTIVE && !job->success) {
        stats->failed++;
        if (job->exception_occurred) {
            if (strcmp (job->exception_type, "cancel") == 0)
                stats->canceled++;
            else if (strcmp (job->exception_type, "timeout") == 0)
                stats->timeout++;
        }
    }
}

static int object_set_integer (json_t *o,
                               const char *key,
                               unsigned int n)
{
    json_t *val = json_integer (n);
    if (!val || json_object_set_new (o, key, val) < 0) {
        json_decref (val);
        return -1;
    }
    return 0;
}

static json_t *job_states_encode (struct job_stats *stats)
{
    unsigned int total = 0;
    json_t *o = json_object ();
    if (!o)
        return NULL;
    for (int i = 1; i < FLUX_JOB_NR_STATES; i++) {
        if (object_set_integer (o,
                                state_index_name (i),
                                stats->state_count[i]) < 0)
            goto error;
        total += stats->state_count[i];
    }
    if (object_set_integer (o, "total", total) < 0)
        goto error;
    return o;
error:
    json_decref (o);
    return NULL;
}

json_t * job_stats_encode (struct job_stats *stats)
{
    json_t *o;
    json_t *states;

    if (!(states = job_states_encode (stats))
        || !(o = json_pack ("{ s:O s:i s:i s:i }",
                            "job_states", states,
                            "failed", stats->failed,
                            "canceled", stats->canceled,
                            "timeout", stats->timeout))) {
        json_decref (states);
        errno = ENOMEM;
        return NULL;
    }
    json_decref (states);
    return o;
}

// vi: ts=4 sw=4 expandtab
