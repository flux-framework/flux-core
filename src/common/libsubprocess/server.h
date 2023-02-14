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
typedef struct subprocess_server subprocess_server_t;

typedef int (*subprocess_server_auth_f) (const flux_msg_t *msg,
		                         void *arg,
					 flux_error_t *error);

/*  Create a subprocess server on the handle `h`. Registers message
 *   handlers, etc for remote execution.
 */
subprocess_server_t *subprocess_server_create (flux_t *h,
                                               const char *local_uri,
                                               uint32_t rank);

/*   Register an authorization function to the subprocess server
 *
 *   The registered function should return 0 to allow the request to
 *    proceed, and -1 with errno set to deny the request.
 */
void subprocess_server_set_auth_cb (subprocess_server_t *s,
                                    subprocess_server_auth_f fn,
                                    void *arg);

/*  Destroy a subprocess server / cleanup subprocess_server_t.  Will
 *  send a SIGKILL to all remaining subprocesses.
 */
void subprocess_server_destroy (subprocess_server_t *s);

/* Send all subprocesses signal and wait up to wait_time seconds for
 * all subprocesses to complete.  This is typically called to send
 * SIGTERM before calling subprocess_server_stop(), allowing
 * users to send a signal to inform subprocesses to complete / cleanup
 * before they are sent SIGKILL.
 *
 * This function will enter the reactor to wait for subprocesses to
 * complete, should only be called on cleanup path when primary
 * reactor has exited.
 */
int subprocess_server_subprocesses_kill (subprocess_server_t *s,
                                         int signum,
                                         double wait_time);

#endif /* !_SUBPROCESS_SERVER_H */
