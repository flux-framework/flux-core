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

#include "ccan/str/str.h"

#include "stats.h"
#include "job_data.h"

static void free_wrapper (void **item)
{
    if (item) {
        free (*item);
        (*item) = NULL;
    }
}

struct job_stats_ctx *job_stats_ctx_create (flux_t *h)
{
    struct job_stats_ctx *statsctx = NULL;

    if (!(statsctx = calloc (1, sizeof (*statsctx))))
        return NULL;
    statsctx->h = h;

    if (!(statsctx->queue_stats = zhashx_new ()))
        goto error;
    zhashx_set_destructor (statsctx->queue_stats, free_wrapper);

    return statsctx;

error:
    job_stats_ctx_destroy (statsctx);
    return NULL;
}

void job_stats_ctx_destroy (struct job_stats_ctx *statsctx)
{
    if (statsctx) {
        int save_errno = errno;
        zhashx_destroy (&statsctx->queue_stats);
        free (statsctx);
        errno = save_errno;
    }
}

static struct job_stats *queue_stats_lookup (struct job_stats_ctx *statsctx,
                                             struct job *job)
{
    struct job_stats *stats = NULL;

    if (!job->queue)
        return NULL;

    stats = zhashx_lookup (statsctx->queue_stats, job->queue);
    if (!stats) {
        if (!(stats = calloc (1, sizeof (*stats))))
            return NULL;
        (void)zhashx_insert (statsctx->queue_stats, job->queue, stats);
    }
    return stats;
}

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
    return flux_job_statetostr ((1<<index), "l");
}

static void stats_add (struct job_stats *stats,
                       struct job *job,
                       flux_job_state_t state)
{
    if (state == FLUX_JOB_STATE_NEW)
        return;

    stats->state_count[state_index (state)]++;

    if (state == FLUX_JOB_STATE_INACTIVE) {
        if (!job->success) {
            if (job->exception_occurred) {
                if (streq (job->exception_type, "cancel"))
                    stats->canceled++;
                else if (streq (job->exception_type, "timeout"))
                    stats->timeout++;
                else
                    stats->failed++;
            }
            else
                stats->failed++;
        }
        else
            stats->successful++;
    }
}

static void stats_update (struct job_stats *stats,
                          struct job *job,
                          flux_job_state_t newstate)
{
    /*  Stats for NEW are not tracked */
    if (job->state != FLUX_JOB_STATE_NEW)
        stats->state_count[state_index (job->state)]--;

    stats_add (stats, job, newstate);
}

void job_stats_update (struct job_stats_ctx *statsctx,
                       struct job *job,
                       flux_job_state_t newstate)
{
    struct job_stats *stats;

    stats_update (&statsctx->all, job, newstate);

    if ((stats = queue_stats_lookup (statsctx, job)))
        stats_update (stats, job, newstate);
}

void job_stats_add_queue (struct job_stats_ctx *statsctx,
                          struct job *job)
{
    struct job_stats *stats;

    if ((stats = queue_stats_lookup (statsctx, job)))
        stats_add (stats, job, job->state);
}

static void stats_purge (struct job_stats *stats, struct job *job)
{
    stats->state_count[state_index (job->state)]--;

    if (!job->success) {
        if (job->exception_occurred) {
            if (streq (job->exception_type, "cancel"))
                stats->canceled--;
            else if (streq (job->exception_type, "timeout"))
                stats->timeout--;
            else
                stats->failed--;
        }
        else
            stats->failed--;
    }
    else
        stats->successful--;
    stats->inactive_purged++;
}

/* An inactive job is being purged, so statistics must be updated.
 */
void job_stats_purge (struct job_stats_ctx *statsctx, struct job *job)
{
    struct job_stats *stats;

    assert (job->state == FLUX_JOB_STATE_INACTIVE);

    stats_purge (&statsctx->all, job);

    if ((stats = queue_stats_lookup (statsctx, job)))
        stats_purge (stats, job);
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

static json_t *stats_encode (struct job_stats *stats, const char *name)
{
    json_t *o;
    json_t *states;

    if (!(states = job_states_encode (stats))
        || !(o = json_pack ("{ s:O s:i s:i s:i s:i s:i }",
                            "job_states", states,
                            "successful", stats->successful,
                            "failed", stats->failed,
                            "canceled", stats->canceled,
                            "timeout", stats->timeout,
                            "inactive_purged", stats->inactive_purged))) {
        json_decref (states);
        errno = ENOMEM;
        return NULL;
    }
    json_decref (states);

    if (name) {
        json_t *no = json_string (name);
        if (!no || json_object_set_new (o, "name", no) < 0) {
            json_decref (no);
            json_decref (o);
            errno = ENOMEM;
            return NULL;
        }
    }
    return o;
}

static json_t *queue_stats_encode (struct job_stats_ctx *statsctx)
{
    struct job_stats *stats;
    json_t *queues;

    if (!(queues = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }

    stats = zhashx_first (statsctx->queue_stats);
    while (stats) {
        const char *name = zhashx_cursor (statsctx->queue_stats);
        json_t *qo = stats_encode (stats, name);
        if (!qo) {
            int save_errno = errno;
            json_decref (queues);
            errno = save_errno;
            return NULL;
        }
        if (json_array_append_new (queues, qo) < 0) {
            json_decref (qo);
            json_decref (queues);
            errno = ENOMEM;
            return NULL;
        }
        stats = zhashx_next (statsctx->queue_stats);
    }

    return queues;
}

json_t * job_stats_encode (struct job_stats_ctx *statsctx)
{
    json_t *o = NULL;
    json_t *queues;

    if (!(o = stats_encode (&statsctx->all, NULL)))
        return NULL;

    if (!(queues = queue_stats_encode (statsctx))) {
        int save_errno = errno;
        json_decref (o);
        errno = save_errno;
        return NULL;
    }

    if (json_object_set_new (o, "queues", queues) < 0) {
        json_decref (queues);
        json_decref (o);
        errno = ENOMEM;
        return NULL;
    }

    return o;
}

// vi: ts=4 sw=4 expandtab
