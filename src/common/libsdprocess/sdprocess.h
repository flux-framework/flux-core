/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDPROCESS_H
#define _SDPROCESS_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sdprocess states, on changes, will lead to calls to callbacks in
 * sdprocess_state ().
 *
 * Possible state changes:
 *
 * init -> active
 * init -> exited
 * active -> exited
 *
 * Note that the active state can be missed, for example:
 * - illegal command passed in by user, systemd exits and never runs
 *   user command
 * - systemd runs user process, but the command errors out before
 *   sdprocess_state_callback() is setup
 * - libsdprocess cannot discern between the above, so the only state
 *   that will be seen in the above two cases is "exited", the
 *   "active" will not be seen.
 */
typedef enum {
    SDPROCESS_INIT = 0,    /* initial state */
    SDPROCESS_ACTIVE = 1,  /* systemd has run process */
    SDPROCESS_EXITED = 2,  /* systemd / process has exited */
} sdprocess_state_t;


typedef struct sdprocess sdprocess_t;

typedef void (*sdprocess_state_f) (sdprocess_t *sdp,
                                   sdprocess_state_t state,
                                   void *arg);

/* return > 0 to end iteration
 * return 0 to continue
 * return < 0 on error
 */
typedef int (*sdprocess_list_f) (flux_t *h,
                                 const char *unitname,
                                 void *arg);

/* Launch process under systemd
 *
 * command must be an absolute path
 * argv & envv & properties arrays are NULL terminated
 * if unnecessary, set fd to < 0
 *
 * Current properties supported:
 * - CPUAffinity=<flux idset> (N.B. input different than systemd)
 * - AllowedCPUs=<flux idset> (N.B. input different than systemd)
 * - MemoryHigh=<bytes> (k, m, g, t suffix allowed)
 *   OR MemoryHigh=<percent> (double + '%')
 *   OR MemoryHigh=infinity (same as 100%)
 * - MemoryMax=<bytes> (k, m, g, t suffix allowed)
 *   OR MemoryMax=<percent> (double + '%')
 *   OR MemoryMax=infinity (same as 100%)
 *
 * Properties not allowed due to internal use:
 * - RemainAfterExit
 *
 * Setup of XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS environment
 * variables are assumed.  If not, systemd will return error.
 */
sdprocess_t *sdprocess_exec (flux_t *h,
                             const char *unitname,
                             char **argv,
                             char **envv,
                             char **properties,
                             int stdin_fd,
                             int stdout_fd,
                             int stderr_fd);

/* Find process already launched under systemd
 *
 * Setup of XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS environment
 * variables are assumed.  If not, systemd will return error.
 */
sdprocess_t *sdprocess_find_unit (flux_t *h, const char *unitname);

/* Setup callback to inform caller of when process enters active or
 * exited state.
 *
 * See comments above noting that active state can be missed.
 */
int sdprocess_state (sdprocess_t *sdp,
                     sdprocess_state_f state_cb,
                     void *arg);

/* Block waiting for process to exit */
int sdprocess_wait (sdprocess_t *sdp);

/* Get unitname of process */
const char *sdprocess_unitname (sdprocess_t *sdp);

/* Get pid of process launched by systemd */
int sdprocess_pid (sdprocess_t *sdp);

/* Determine if process is active */
bool sdprocess_active (sdprocess_t *sdp);

/* Determine if process has exited */
bool sdprocess_exited (sdprocess_t *sdp);

/* exit status (systemd1 ExecMainStatus)
 * - typically exit code from process
 *   OR
 *   signal number if signaled
 * - 200-243 special exit status from systemd
 *   - 203 = exec error
 */
int sdprocess_exit_status (sdprocess_t *sdp);

/* wait status as would be returned from wait(2)
 */
int sdprocess_wait_status (sdprocess_t *sdp);

/* Send signal to process.  Can return EPERM if not yet ready to be
 * signaled.
 */
int sdprocess_kill (sdprocess_t *sdp, int signo);

/* Cleanup data cached in systemd
 * - note that this is different than sdprocess_destroy()
 * - can return EAGAIN if not yet ready for cleanup
 *
 * Once sdprocess_systemd_cleanup() is executed successfully, callers
 * cannot expect sdprocess functions to behave consistently.  For
 * example, sdprocess_active() will no longer function correctly after
 * sdprocess_systemd_cleanup() is called.  Typically, this will be
 * called right before sdprocess_destroy() is called.
 */
int sdprocess_systemd_cleanup (sdprocess_t *sdp);

void sdprocess_destroy (sdprocess_t *sdp);

/* list systemd units for current user
 * - flux_t optional, only used for logging
 * - optionally filter with fnmatch(3) pattern
 */
int sdprocess_list (flux_t *h,
                    const char *pattern,
                    sdprocess_list_f list_cb,
                    void *arg);

#ifdef __cplusplus
}
#endif

#endif /* !_SDPROCESS_H */
