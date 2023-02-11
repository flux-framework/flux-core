/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_SERVER_H
#define _SUBPROCESS_SERVER_H

#include "subprocess.h"

/*  flux_subprocess_server_t: Handler for a subprocess remote server */
typedef struct flux_subprocess_server flux_subprocess_server_t;

typedef int (*flux_subprocess_server_auth_f) (const flux_msg_t *msg,
                                              void *arg);


int server_start (flux_subprocess_server_t *s);

void server_stop (flux_subprocess_server_t *s);

int server_signal_subprocesses (flux_subprocess_server_t *s, int signum);

int server_terminate_subprocesses (flux_subprocess_server_t *s);

int server_terminate_by_uuid (flux_subprocess_server_t *s,
                              const char *id);

int server_terminate_setup (flux_subprocess_server_t *s,
                            double wait_time);

void server_terminate_cleanup (flux_subprocess_server_t *s);

int server_terminate_wait (flux_subprocess_server_t *s);


/*  Start a subprocess server on the handle `h`. Registers message
 *   handlers, etc for remote execution.
 */
flux_subprocess_server_t *flux_subprocess_server_start (flux_t *h,
                                                        const char *local_uri,
                                                        uint32_t rank);

/*   Register an authorization function to the subprocess server
 *
 *   The registered function should return 0 to allow the request to
 *    proceed, and -1 with errno set to deny the request.
 */
void flux_subprocess_server_set_auth_cb (flux_subprocess_server_t *s,
                                         flux_subprocess_server_auth_f fn,
                                         void *arg);

/*  Stop a subprocess server / cleanup flux_subprocess_server_t.  Will
 *  send a SIGKILL to all remaining subprocesses.
 */
void flux_subprocess_server_stop (flux_subprocess_server_t *s);

/* Send all subprocesses signal and wait up to wait_time seconds for
 * all subprocesses to complete.  This is typically called to send
 * SIGTERM before calling flux_subprocess_server_stop(), allowing
 * users to send a signal to inform subprocesses to complete / cleanup
 * before they are sent SIGKILL.
 *
 * This function will enter the reactor to wait for subprocesses to
 * complete, should only be called on cleanup path when primary
 * reactor has exited.
 */
int flux_subprocess_server_subprocesses_kill (flux_subprocess_server_t *s,
                                              int signum,
                                              double wait_time);

/* Terminate all subprocesses started by a sender id */
int flux_subprocess_server_terminate_by_uuid (flux_subprocess_server_t *s,
                                              const char *id);

#endif /* !_SUBPROCESS_SERVER_H */
