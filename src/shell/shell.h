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

typedef void (flux_shell_task_io_f) (flux_shell_task_t *task,
                                     const char *stream,
                                     void *arg);

int flux_shell_aux_set (flux_shell_t *shell,
                        const char *name,
                        void *aux,
                        flux_free_f free_fn);

void * flux_shell_aux_get (flux_shell_t *shell, const char *name);

/*
 *  Take a "completion reference" on the shell object `shell`.
 *  This function takes a named reference on the shell so that it will
 *  not consider a job "complete" until the reference is released with
 *  flux_shell_remove_completion_ref().
 *
 *  Returns the reference count for the particular name, or -1 on error.
 */
int flux_shell_add_completion_ref (flux_shell_t *shell,
                                   const char *fmt, ...);

/*
 *  Remove a named "completion reference" for the shell object `shell`.
 *  Once all references have been removed, the shells reactor is stopped
 *  with flux_reactor_stop (shell->r).
 *
 *  Returns 0 on success, -1 on failure.
 */
int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                      const char *fmt, ...);

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

/*  flux_shell_task_t API:
 */

/*  Return the current task for task_init, task_exec, and task_exit callbacks:
 *
 *  Returns NULL in any other context.
 */
flux_shell_task_t * flux_shell_current_task (flux_shell_t *shell);

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
