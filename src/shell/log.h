/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_LOG_H
#define _SHELL_LOG_H

#include <stdio.h>
#include <flux/shell.h>

/*  Initialize/finalize shell internal log facility:
 */
int shell_log_init (flux_shell_t *shell, const char *progname);
void shell_log_fini (void);

/*  Reinitialize shell log in case verbosity has changed:
 */
int shell_log_reinit (flux_shell_t *shell);

/*  Adjust internal logging level
 */
void shell_log_set_level (int level);

/*  Let logging system know an exception is already logged,
 *   no need to log another.
 */
void shell_log_set_exception_logged (void);

/*  Shell log function compatible with libutil llog interface
 */
void shell_llog (void *arg,
                 const char *file,
                 int line,
                 const char *func,
                 const char *subsys,
                 int level,
                 const char *fmt,
                 va_list ap);

#endif /* !_SHELL_RC_H */

/* vi: ts=4 sw=4 expandtab
 */

