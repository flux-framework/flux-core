/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_SHELL_H
#define FLUX_SHELL_H

#include <flux/core.h>
#include <flux/optparse.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_shell flux_shell_t;
typedef struct shell_task flux_shell_task_t;

typedef void (*flux_shell_task_io_f) (flux_shell_task_t *task,
                                      const char *stream,
                                      void *arg);

/*  Get flux_shell_t object from flux plugin handle
 */
flux_shell_t * flux_plugin_get_shell (flux_plugin_t *p);

/* flux_shell_t interface */

int flux_shell_aux_set (flux_shell_t *shell,
                        const char *name,
                        void *aux,
                        flux_free_f free_fn);

void * flux_shell_aux_get (flux_shell_t *shell, const char *name);

/*  Get value of an environment variable from the global job environment
 */
const char * flux_shell_getenv (flux_shell_t *shell, const char *name);

/*  Get environ job environment as a JSON string. Caller must free.
 */
int flux_shell_get_environ (flux_shell_t *shell, char **json_str);

/*  Set an environment variable in the global job environment
 */
int flux_shell_setenvf (flux_shell_t *shell, int overwrite,
                        const char *name, const char *fmt, ...)
                        __attribute__ ((format (printf, 4, 5)));

/*  Unset an environment variable in the global job environment
 */
int flux_shell_unsetenv (flux_shell_t *shell, const char *name);


/*  Return shell info as a JSON string.
 *  {
 *   "jobid":I,
 *   "rank":i,
 *   "size":i,
 *   "ntasks";i,
 *   "options": { "verbose":b, "standalone":b },
 *   "jobspec":o,
 *   "R":o
 *  }
 */
int flux_shell_get_info (flux_shell_t *shell, char **json_str);


/*  Return rank and task info for given shell rank as JSON string.
 *  {
 *   "broker_rank":i,
 *   "ntasks":i
 *   "resources": { "cores":s, ... }
 *  }
 */
int flux_shell_get_rank_info (flux_shell_t *shell,
                              int shell_rank,
                              char **json_str);

/*
 *  Take a "completion reference" on the shell object `shell`.
 *  This function takes a named reference on the shell so that it will
 *  not consider a job "complete" until the reference is released with
 *  flux_shell_remove_completion_ref().
 *
 *  Returns the reference count for the particular name, or -1 on error.
 */
int flux_shell_add_completion_ref (flux_shell_t *shell,
                                   const char *fmt, ...)
                                   __attribute__ ((format (printf, 2, 3)));

/*
 *  Remove a named "completion reference" for the shell object `shell`.
 *  Once all references have been removed, the shells reactor is stopped
 *  with flux_reactor_stop (shell->r).
 *
 *  Returns 0 on success, -1 on failure.
 */
int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                      const char *fmt, ...)
                                      __attribute__ ((format (printf, 2, 3)));

/*  Send signal `sig` to all currently running tasks in shell.
 */
void flux_shell_killall (flux_shell_t *shell, int sig);


/*  Add an event handler for shell event `subtopic`. Message handler
 *  will be auto-destroyed on flux handle destruction at shell shutdown.
 */
int flux_shell_add_event_handler (flux_shell_t *shell,
                                  const char *subtopic,
                                  flux_msg_handler_f cb,
                                  void *arg);

/*  Register service handler for `method` with the shell
 */
int flux_shell_service_register (flux_shell_t *shell,
                                 const char *method,
                                 flux_msg_handler_f cb,
                                 void *arg);

/*  Send an rpc to shell `method` by shell rank
 */
flux_future_t *flux_shell_rpc_pack (flux_shell_t *shell,
                                    const char *method,
                                    int shell_rank,
                                    int flags,
                                    const char *fmt, ...);

/*  Call into shell plugin stack
 */
int flux_shell_plugstack_call (flux_shell_t *shell,
                               const char *topic,
                               flux_plugin_arg_t *args);

/*  flux_shell_task_t API:
 */

/*  Return the current task for task_init, task_exec, and task_exit callbacks:
 *
 *  Returns NULL in any other context.
 */
flux_shell_task_t * flux_shell_current_task (flux_shell_t *shell);

/*  Return task general information as JSON string:
 *  {
 *    "localid":i,
 *    "rank":i,
 *    "state":s,
 *    "pid":I,
 *    "wait_status":i,
 *    "exitcode":i,
 *    "signaled":i
 *  }
 *  Where 'pid' is only valid when task is running, and 'wait_status',
 *  'exitcode' and 'signaled' are only valid when task has exited.
 */
int flux_shell_task_get_info (flux_shell_task_t *task, char **json_str);

/*
 *  Return the cmd structure for a shell task.
 */
flux_cmd_t *flux_shell_task_cmd (flux_shell_task_t *task);

/*
 *  Return the subprocess for a shell task in task_fork, task_exit callbacks:
 */
flux_subprocess_t *flux_shell_task_subprocess (flux_shell_task_t *task);

/*  Call `cb` when channel `name` is ready for reading.
 *
 *  Callback can then call flux_shell_task_get_subprocess() and use
 *   flux_subprocess_read() or getline() on the result to get
 *   available data.
 *
 *  Only one subscriber per stream is allowed. If subscribe is called
 *   on a stream with an existing subscriber then -1 is returned with
 *   errno set to EEXIST.
 */
int flux_shell_task_channel_subscribe (flux_shell_task_t *task,
                                       const char *channel,
                                       flux_shell_task_io_f cb,
                                       void *arg);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_SHELL_H */
