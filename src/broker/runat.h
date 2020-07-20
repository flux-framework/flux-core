/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Execute list of commands, sequentially, by name.
 */

#ifndef _BROKER_RUNAT_H
#define _BROKER_RUNAT_H

struct runat;

typedef void (*runat_completion_f)(struct runat *r,
                                   const char *name,
                                   void *arg);

struct runat *runat_create (flux_t *h,
                            const char *local_uri,
                            runat_completion_f cb,
                            void *arg);
void runat_destroy (struct runat *r);

/* Push command, to be run under shell -c, onto named list.
 * If log_stdio is true, stdout/stderr go to flux_log (o/w combine w/broker)
 */
int runat_push_shell_command (struct runat *r,
                              const char *name,
                              const char *cmdline,
                              bool log_stdio);

/* Push interactive shell onto named list.
 */
int runat_push_shell (struct runat *r, const char *name);

/* Push command, to be run directly, onto named list.
 * The command is specified by argz.
 * If log_stdio is true, stdout/stderr go to flux_log (o/w combine w/broker)
 */
int runat_push_command (struct runat *r,
                        const char *name,
                        const char *argz,
                        size_t argz_len,
                        bool log_stdio);

/* Get exit code of completed command list.
 * If multiple commands fail, the exit code is that of the first failure.
 */
int runat_get_exit_code (struct runat *r, const char *name, int *rc);

/* Begin execution of named list.
 * Completion callback is called once command finish execution.
 * The completion callback may call runat_get_exit_code().
 */
int runat_start (struct runat *r, const char *name);

/* Abort execution of named list.
 * If a command is running, it is signaled.
 */
int runat_abort (struct runat *r, const char *name);

/* Test whether named list has been defined.
 */
bool runat_is_defined (struct runat *r, const char *name);

/* Test whether named list has completed running.
 */
bool runat_is_completed (struct runat *r, const char *name);

#endif /* !_BROKER_RUNAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
