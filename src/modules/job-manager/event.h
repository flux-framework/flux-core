/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_EVENT_H
#define _FLUX_JOB_MANAGER_EVENT_H

#include <stdarg.h>
#include <flux/core.h>
#include <jansson.h>

#include "job.h"
#include "job-manager.h"

enum job_manager_event_flags {

    /*  EVENT_NO_COMMIT events are the same as any other event, except
     *   that the event is not posted to the job eventlog in the KVS.
     */
    EVENT_NO_COMMIT = 1,
};

/* Take any action for 'job' currently needed based on its internal state.
 * Returns 0 on success, -1 on failure with errno set.
 * This function is idempotent.
 */
int event_job_action (struct event *event, struct job *job);

/* Call to update 'job' internal state based on 'event'.
 * Returns 0 on success, -1 on failure with errno set.
 */
int event_job_update (struct job *job, json_t *event);

/* Add notification of job's state transition to its current state and
 * the timestamp of the change to batch for publication.
 */
int event_batch_pub_state (struct event *event, struct job *job,
                           double timestamp);

/* Add add response to batch, to be sent upon batch completion.
 */
int event_batch_respond (struct event *event, const flux_msg_t *msg);

/* Add job to batch, job event handling will be paused until batch completion.
 */
int event_batch_add_job (struct event *event, struct job *job);

/* Post event 'name' and optionally 'context' to 'job'.
 * Internally, calls event_job_update(), then event_job_action(), then commits
 * the event to job KVS eventlog.  The KVS commit completes asynchronously.
 * Returns 0 on success, -1 on failure with errno set.
 */
int event_job_post_pack (struct event *event,
                         struct job *job,
                         const char *name,
                         int flags,
                         const char *context_fmt,
                         ...);

int event_job_post_vpack (struct event *event,
                          struct job *job,
                          const char *name,
                          int flags,
                          const char *context_fmt,
                          va_list ap);

int event_job_post_entry (struct event *event,
                          struct job *job,
                          int flags,
                          json_t *entry);

void event_ctx_destroy (struct event *event);
struct event *event_ctx_create (struct job_manager *ctx);

void event_listeners_disconnect_rpc (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg);


/*  Return globally unique index for event name */
int event_index (struct event *event, const char *name);

#endif /* _FLUX_JOB_MANAGER_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

