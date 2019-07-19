/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_H
#define _SHELL_H

#include <czmq.h>
#include <flux/core.h>
#include <flux/optparse.h>

/* Later this typedef may be exported publicly. e.g. to shell plugins,
 * but for now keep it internal to avoid the need for another header.
 */
typedef struct flux_shell flux_shell_t;

struct flux_shell {
    flux_jobid_t jobid;
    int broker_rank;

    optparse_t *p;
    flux_t *h;
    flux_reactor_t *r;

    struct shell_info *info;
    struct shell_svc *svc;
    struct shell_io *io;
    struct shell_pmi *pmi;
    zlist_t *tasks;

    zhashx_t *completion_refs;

    int rc;

    bool verbose;
    bool standalone;
};


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

#endif /* !_SHELL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
