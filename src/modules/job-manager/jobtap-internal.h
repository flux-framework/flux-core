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
                     flux_error_t *errp);

int jobtap_call_create (struct jobtap *jobtap,
                        struct job *job,
                        flux_error_t *errp);

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
                               flux_error_t *errp);

/*  Call `job.update.<key>` callback to verify that a jobspec update of
 *  'key' to 'value' is allowed. The flux_msg_cred parameter should be set
 *  to the credentials of the original requestor.
 *
 *  Returns an error with 'errp' set if no plugin is registered to handle
 *  updates of 'key', or if the callback returned an error.
 *
 *  If the update needs further validation via `job.validate`, then
 *  needs_validation will be set nonzero. The caller should be sure to pass
 *  the updated jobspec to `job.validate` before posting updates to the job
 *  eventlog.
 *
 *  If the update requires a feasibility check with the scheduler, then
 *  require_feasibility will be set nonzero. The caller should attempt to
 *  request a feasibility check before applying updates.
 */
int jobtap_job_update (struct jobtap *jobtap,
                       struct flux_msg_cred cred,
                       struct job *job,
                       const char *key,
                       json_t *value,
                       int *needs_validation,
                       int *needs_feasibility,
                       json_t **updates,
                       flux_error_t *errp);

/*  Call the `job.validate` plugin stack, but using an updated jobspec by
 *  applying 'updates' to 'job'.
 *
 *  If validation fails, then this function will return -1 with the error
 *  set in 'errp'.
 */
int jobtap_validate_updates (struct jobtap *jobtap,
                             struct job *job,
                             json_t *updates,
                             flux_error_t *errp);

/*  Load a new jobtap from `path`. Path may start with `builtin.` to
 *   attempt to load one of the builtin jobtap plugins.
 */
flux_plugin_t * jobtap_load (struct jobtap *jobtap,
                             const char *path,
                             json_t *conf,
                             flux_error_t *errp);

typedef int (*jobtap_builtin_f) (flux_plugin_t *p, void *arg);

/*  Add a new jobtap builtin plugin.
 *  Allows builtins to be created externally to the jobtap module.
 */
int jobtap_register_builtin (struct jobtap *jobtap,
                             const char *name,
                             jobtap_builtin_f init_cb,
                             void *arg);

/*  Job manager RPC handler for loading new jobtap plugins.
 */
void jobtap_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg);

/*  Job manager RPC handler for querying jobtap plugin data.
 */
void jobtap_query_handler (flux_t *h,
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

