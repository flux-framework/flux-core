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

#ifndef BROKER_RUNAT_H
#define BROKER_RUNAT_H

enum {
    RUNAT_FLAG_LOG_STDIO = 1,   /* stdout/stderr go to flux_log (o/w
                                 * combine w/ broker) */
    RUNAT_FLAG_FORK_EXEC = 2,  /* require use of fork/exec, not
                                * posix_spawn */
    RUNAT_FLAG_NO_SETPGRP = 4,  /* Do not run process in its own pgrp */
};

struct runat;

typedef void (*runat_completion_f)(struct runat *r,
                                   const char *name,
                                   void *arg);

typedef void (*runat_notify_f)(void *handle, const char *msg);

struct runat *runat_create (flux_t *h,
                            const char *local_uri,
                            const char *jobid,
                            runat_notify_f notify_cb,
                            void *notify_handle);

void runat_destroy (struct runat *r);

/* Push command, to be run under shell -c, onto named list.
 */
int runat_push_shell_command (struct runat *r,
                              const char *name,
                              const char *cmdline,
                              int flags);

/* Push interactive shell onto named list.
 * Note: RUNAT_FLAG_LOG_STDIO flag not allowed
 */
int runat_push_shell (struct runat *r,
                      const char *name,
                      const char *shell,
                      int flags);

/* Push command, to be run directly, onto named list.
 * The command is specified by argz.
 */
int runat_push_command (struct runat *r,
                        const char *name,
                        const char *argz,
                        size_t argz_len,
                        int flags);

/* Get exit code of completed command list.
 * If multiple commands fail, the exit code is that of the first failure.
 */
int runat_get_exit_code (struct runat *r, const char *name, int *rc);

/* Begin execution of named list.
 * Completion callback is called once command finish execution.
 * The completion callback may call runat_get_exit_code().
 */
int runat_start (struct runat *r,
                 const char *name,
                 runat_completion_f cb,
                 void *arg);

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

/* Test whether named list contains interactive commands.
 */
bool runat_is_interactive (struct runat *r, const char *name);

// only for unit testing
struct runat *runat_create_test (flux_t *h,
                                 const char *local_uri,
                                 const char *jobid,
                                 runat_notify_f notify_cb,
                                 void *notify_handle);


#endif /* !BROKER_RUNAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
