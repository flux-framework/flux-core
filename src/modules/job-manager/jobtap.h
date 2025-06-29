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

/*  Extended version of above with capability of registering a service that
 *   guests can access.
 */
int flux_jobtap_service_register_ex (flux_plugin_t *p,
                                     const char *method,
                                     uint32_t rolemask,
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

/*  Convenience function to set 'errstr' in return args to formatted message.
 *   returns -1 to allow an idiom like
 *
 *   return flux_jobtap_error (p, args, "error message");
 */
int flux_jobtap_error (flux_plugin_t *p,
                       flux_plugin_arg_t *args,
                       const char *fmt, ...)
                       __attribute__ ((format (printf, 3, 4)));

/*  Convenience function to be used in job.validate callback to reject a job.
 *   Identical to flux_jobtap_error () except if 'fmt' is NULL, a default
 *   message of the form "rejected by job-manager plugin <name>" is substituted.
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

/*  Set a named flag on job `id`.
 */
int flux_jobtap_job_set_flag (flux_plugin_t *p,
                              flux_jobid_t id,
                              const char *flag);


/*  Raise an exception for job 'id' or current job if FLUX_JOBTAP_CURRENT_JOB
 */
int flux_jobtap_raise_exception (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 const char *type,
                                 int severity,
                                 const char *fmt,
                                 ...);

/*  Post event 'name' to job `id` with optional context defined by
 *   args `fmt, ...`.
 *
 *  If `id` is FLUX_JOBTAP_CURRENT_JOB then the event will be posted to
 *   the current job.
 */
int flux_jobtap_event_post_pack (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 const char *name,
                                 const char *fmt,
                                 ...);

/*  Post a request to update jobspec for the current job. Accumulated updates
 *  will be applied once the current callback returns so that the jobspec is
 *  not modified while the plugin may have references to it.
 *
 *  The update is defined by one or more period-delimited keys and values
 *  specified by `fmt`, ..., e.g.
 *
 *      flux_jobtap_jobspec_update_pack (p,
 *                                       "{s:i s:s}",
 *                                       "attributes.system.duration", 3600,
 *                                       "attributes.system.queue", "batch");
 *
 *  Returns -1 with errno set to EINVAL for invalid arguments, if there is
 *  no current job, or if the current job is in RUN, CLEANUP, or INACTIVE
 *  states.
 */
int flux_jobtap_jobspec_update_pack (flux_plugin_t *p, const char *fmt, ...);

/*  Similar to flux_jobtap_jobspec_update_pack(), but asynchronously update
 *  a specific jobid. This version assumes the job is quiescent, so the event
 *  is applied immediately.
 *
 *  Returns -1 with errno set to EINVAL for invalid arguments, if the job
 *  does not exist, if the target job is in RUN, CLEANUP or INACTIVE states,
 *  or if the function is called from a jobtap callback for the target job.
 */
int flux_jobtap_jobspec_update_id_pack (flux_plugin_t *p,
                                        flux_jobid_t id,
                                        const char *fmt,
                                        ...);

/*  Return a flux_plugin_arg_t object for a job.
 *
 *  The result can then be unpacked with flux_plugin_arg_unpack(3) to get
 *   active job information such as userid, state, etc.
 *
 *  If `id` is not an active job or is not found in the job manager's
 *   inactive job cache then NULL is returned with ENOENT set.
 *
 *  Caller must free with flux_plugin_arg_destroy(3).
 */
flux_plugin_arg_t * flux_jobtap_job_lookup (flux_plugin_t *p,
                                            flux_jobid_t id);


int flux_jobtap_get_job_result (flux_plugin_t *p,
                                flux_jobid_t id,
                                flux_job_result_t *resultp);

/*  Return 1 if event 'name' has been posted to job 'id', 0 if not.
 */
int flux_jobtap_job_event_posted (flux_plugin_t *p,
                                  flux_jobid_t id,
                                  const char *name);

/*
 *  This function subscribes plugin 'p' to extra callbacks for job 'id'
 *   such as job.event.<name> for each job event. The plugin must have a
 *   handler registered that matches one or more of these callbacks.
 */
int flux_jobtap_job_subscribe (flux_plugin_t *p, flux_jobid_t id);


/*  Unsubscribe plugin 'p' from extra callbacks for job 'id'
 */
void flux_jobtap_job_unsubscribe (flux_plugin_t *p, flux_jobid_t id);


/*  Post an event to the current job eventlog indicating that a prolog
 *   action has started. This will block the start request to the
 *   execution system until `flux_jobtap_prolog_finish()` is called.
 */
int flux_jobtap_prolog_start (flux_plugin_t *p, const char *description);

/*  Post an event to the eventlog for job id indicating that a prolog
 *   action has finished. The description should match the description
 *   of an outstanding prolog start event. `status` is informational
 *   and should be 0 to indicate success, non-zero for failure.
 */
int flux_jobtap_prolog_finish (flux_plugin_t *p,
                               flux_jobid_t id,
                               const char *description,
                               int status);

/*  Post an event to the current job eventlog indicating that an epilog
 *   action has started. This will block the free request to the
 *   scheduler until `flux_jobtap_epilog_finish()` is called.
 */
int flux_jobtap_epilog_start (flux_plugin_t *p, const char *description);

/*  Post an event to the eventlog for job id indicating that an epilog
 *   action has finished. The description should match the description
 *   of an outstanding epilog start event. `status` is informational
 *   and should be 0 to indicate success, non-zero for failure.
 */
int flux_jobtap_epilog_finish (flux_plugin_t *p,
                               flux_jobid_t id,
                               const char *description,
                               int status);

/*  Call jobtap plugin stack for `topic` from another plugin using jobid `id`.
 *   Note that plugins will only have access to arguments in `args` passed
 *   here, i.e. the implementation does not automatically add job data as
 *   documented in other jobtap callbacks. A job id is required here since
 *   much of the jobtap API assumes it is operating on a current job.
 *   The `id` argument may be set to FLUX_JOBTAP_CURRENT_JOB to keep the same
 *   current job as in the context of the caller.
 *
 *   The `topic` string may not begin with the prefix `job.`.
 */
int flux_jobtap_call (flux_plugin_t *p,
                      flux_jobid_t id,
                      const char *topic,
                      flux_plugin_arg_t *args);
#ifdef __cplusplus
}
#endif

#endif /* !FLUX_JOBTAP_H */
