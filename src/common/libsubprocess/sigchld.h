/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_SIGCHLD_H
#define _LIBSUBPROCESS_SIGCHLD_H

#include <flux/core.h>

/* Call sigchld_initialize () before spawn.  A SIGCHLD handler is registered
 * on the first call.  Subsequent calls increase its reference count.
 */
int sigchld_initialize (flux_reactor_t *r);

/* Call sigchld_finalize () after exit.  Each call decreases the SIGCHLD
 * handler reference count.  The handler is is unregistered when the
 * count reaches zero.
 */
void sigchld_finalize (void);

/* Callback on process status change.
 */
typedef void (*sigchld_f)(pid_t pid, int status, void *arg);

/* Register a callback for process status changes on pid.
 * Call immediately after spawn (don't let the reactor run in between).
 */
int sigchld_register (flux_reactor_t *r, pid_t pid, sigchld_f cb, void *arg);

/* Unregister callback.
 */
void sigchld_unregister (pid_t pid);

#endif /* !_LIBSUBPROCESS_SIGCHLD_H */

// vi:ts=4 sw=4 expandtab
