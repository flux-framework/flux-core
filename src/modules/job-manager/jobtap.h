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

#define FLUX_JOBTAP_CURRENT_JOB FLUX_JOBID_ANY

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

/*  Start a loop to re-prioritize all jobs. The plugin "priority.get"
 *   callback will be called for each job currently in SCHED or
 *   PRIORITY states.
 */
int flux_jobtap_reprioritize_all (flux_plugin_t *p);

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


/*  Convenience function to be used in job.validate callback to reject a
 *   job with error message formatted by the fmt string.
 *   returns -1 to allow an idiom like:
 *
 *   return flux_jobtap_reject_job (p, args, "failed validation");
 */
int flux_jobtap_reject_job (flux_plugin_t *p,
                            flux_plugin_arg_t *args,
                            const char *fmt, ...)
                            __attribute__ ((format (printf, 3, 4)));


/*  Add a job dependency to a job with the given description. The dependency
 *   will keep the job in the DEPEND state until it is removed later via the
 *   flux_jobtap_dependency_remove() function.
 *
 *  This function is only valid from the job.state.depend callback.
 *
 *  A job dependency may only be added to a job once.
 *
 *  Returns 0 on success, or -1 on error with errno set:
 *   - ENOENT: job 'id' not found
 *   - EEXIST: dependency 'description' has already been used
 *   - EINVAL: invalid argument
 *
 */
int flux_jobtap_dependency_add (flux_plugin_t *p,
                                flux_jobid_t id,
                                const char *description);

/*  Remove a currently active job dependency with the given description.
 *   Once all outstanding job dependencies have been removed, then the job
 *   will proceed out of the DEPEND state.
 *
 *  Returns 0 on success, -1 on failure with errno set:
 *   ENOENT: job 'id' not found or 'description' is not a current dependency
 *   EINVAL: invalid argument
 *
 */
int flux_jobtap_dependency_remove (flux_plugin_t *p,
                                   flux_jobid_t id,
                                   const char *description);


/*  Set plugin-specific data to an individual job by name.
 *   The optional destructor, free_fn, will be called when `name` is
 *   overwritten by a new value, or when the job is complete and its
 *   underlying job object is destroyed.
 *
 *  If `id` is FLUX_JOBTAP_CURRENT_JOB, then the aux data will be attached
 *   to the currently processed job, if any.
 */
int flux_jobtap_job_aux_set (flux_plugin_t *p,
                             flux_jobid_t id,
                             const char *name,
                             void *val,
                             flux_free_f free_fn);

/*  Get plugin-specific data from a job.
 *
 *  If `id` is FLUX_JOBTAP_CURRENT_JOB, then the current job will be used.
 */
void * flux_jobtap_job_aux_get (flux_plugin_t *p,
                                flux_jobid_t id,
                                const char *name);


/*  Delete plugin specific data `val` from a job.
 *
 *  If `id` is FLUX_JOBTAP_CURRENT_JOB then the current job will be used.
 */
int flux_jobtap_job_aux_delete (flux_plugin_t *p,
                                flux_jobid_t id,
                                void *val);

/*  Raise an exception for job 'id' or current job if FLUX_JOBTAP_CURRENT_JOB
 */
int flux_jobtap_raise_exception (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 const char *type,
                                 int severity,
                                 const char *fmt,
                                 ...);

/*  Return a flux_plugin_arg_t object for any active job.
 *
 *  The result can then be unpacked with flux_plugin_arg_unpack(3) to get
 *   active job information such as userid, state, etc.
 *
 *  If `id` is not an active job then NULL is returned with ENOENT set.
 *
 *  Caller must free with flux_plugin_arg_destroy(3).
 */
flux_plugin_arg_t * flux_jobtap_job_lookup (flux_plugin_t *p,
                                            flux_jobid_t id);
#ifdef __cplusplus
}
#endif

#endif /* !FLUX_JOBTAP_H */
