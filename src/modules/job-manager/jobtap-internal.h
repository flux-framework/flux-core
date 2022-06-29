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

#include "src/common/libflux/types.h" /* flux_error_t */

#include "job.h"
#include "job-manager.h"

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


/*  Jobtap call specific to validating a job during submission. If the
 *   plugin returns failure from this callback the job will be rejected
 *   with an optional error message passed back in `errp`.
 */
int jobtap_validate (struct jobtap *jobtap,
                     struct job *job,
                     char **errp);

int jobtap_call_create (struct jobtap *jobtap,
                        struct job *job,
                        char **errp);

/*  Jobtap call to iterate attributes.system.dependencies dictionary
 *   and call job.dependency.<schema> for each entry.
 *
 *  If there is no plugin registered to handle a given scheme, then
 *   if raise_exception is true a nonfatal job exception is raised,
 *   otherwise an error is returned. A plugin which handles a given schema
 *   may also reject the job if the dependency stanza has errors.
 */
int jobtap_check_dependencies (struct jobtap *jobtap,
                               struct job *job,
                               bool raise_exception,
                               char **errp);


/*  Load a new jobtap from `path`. Path may start with `builtin.` to
 *   attempt to load one of the builtin jobtap plugins.
 */
flux_plugin_t * jobtap_load (struct jobtap *jobtap,
                             const char *path,
                             json_t *conf,
                             flux_error_t *errp);

/*  Job manager RPC handler for loading new jobtap plugins.
 */
void jobtap_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg);


int jobtap_notify_subscribers (struct jobtap *jobtap,
                               struct job *job,
                               const char *event_name,
                               const char *fmt,
                               ...);

#endif /* _FLUX_JOB_MANAGER_JOBTAP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

