/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux job-manager "jobtap" plugin interface
 */

#ifndef FLUX_JOBTAP_H
#define FLUX_JOBTAP_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Get a copy of the flux_t handle which may then be used to
 *   set timer, periodic, prep/check/idle watchers, etc.
 */
flux_t * flux_jobtap_get_flux (flux_plugin_t *p);

/*  Register a service handler for `method` within the job-manager.
 *   The service will be 'job-manager.<name>.<method>'
 */
int flux_jobtap_service_register (flux_plugin_t *p,
                                  const char *method,
                                  flux_msg_handler_f cb,
                                  void *arg);

/*  Set the priority of job `id` to `priority`. This does nothing
 *   if the priority isn't changed from the current value or if the
 *   job is not in the PRIORITY or SCHED states. O/w, a priority event
 *   is generated, job->priority is updated, and the job may move from
 *   PRIORITY->SCHED state.
 */
int flux_jobtap_reprioritize_job (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  unsigned int priority);

/*  Convenience function to return unavailable priority in PRIORITY state
 */
int flux_jobtap_priority_unavail (flux_plugin_t *p,
                                  flux_plugin_arg_t *args);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_JOBTAP_H */
