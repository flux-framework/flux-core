/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_JOBTAP_H
#define _FLUX_JOB_MANAGER_JOBTAP_H

#include "job.h"
#include "job-manager.h"

typedef struct jobtap_error {
    char text [128];
} jobtap_error_t;

struct jobtap * jobtap_create (struct job_manager *ctx);

void jobtap_destroy (struct jobtap *jobtap);

/*  Call the jobtap plugin with topic string `topic`.
 *   `fmt` is jansson-style pack arguments to add the plugin args
 */
int jobtap_call (struct jobtap *jobtap,
                 struct job *job,
                 const char *topic,
                 const char *fmt,
                 ...);

/*  Jobtap call specific for getting a new priority from the jobtap
 *   plugin if available. The priority will be returned in `pprio` if
 *   it was set.
 */
int jobtap_get_priority (struct jobtap *jobtap,
                         struct job *job,
                         int64_t *pprio);

/*  Load a new jobtap from `path`. Path may start with `builtin.` to
 *   attempt to load one of the builtin jobtap plugins.
 */
int jobtap_load (struct jobtap *jobtap,
                 const char *path,
                 jobtap_error_t *errp);

/*  Job manager RPC handler for loading new jobtap plugins.
 */
void jobtap_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg);


#endif /* _FLUX_JOB_MANAGER_JOBTAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

