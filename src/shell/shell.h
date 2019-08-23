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

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_SHELL_H */
