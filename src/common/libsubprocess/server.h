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

typedef struct subprocess_server subprocess_server_t;

typedef int (*subprocess_server_auth_f) (const flux_msg_t *msg,
                                         void *arg,
                                         flux_error_t *error);

/* Create a subprocess server.
 * This sets up a signal watcher for SIGCHLD.  Make sure SIGCHLD cannot be
 * delivered to other threads. Also, it may be wise to block SIGPIPE to
 * avoid termination when writing to stdin of a subprocess that has terminated.
 */
subprocess_server_t *subprocess_server_create (flux_t *h,
                                               const char *service_name,
                                               const char *local_uri,
                                               subprocess_log_f log_fn,
                                               void *log_data);

/* Register a callback to allow/deny each rexec request.
 * The callback should return 0 to allow.  It should return -1 with a
 * message in 'error' to deny.
 */
void subprocess_server_set_auth_cb (subprocess_server_t *s,
                                    subprocess_server_auth_f fn,
                                    void *arg);

/* Destroy a subprocess server.  This sends a SIGKILL to any remaining
 * subprocesses, then destroys them.
 */
void subprocess_server_destroy (subprocess_server_t *s);

/* Send all subprocesses a signal and return a future that is fulfilled
 * when all subprocesses have exited.  New rexec.exec requests will fail.
 * This future is fulfilled immediately if there are no subprocesses, but if
 * there are some, the orig. reactor must be allowed to run in order for the
 * shutdown to make progress.  Therefore this future should be tracked with
 * flux_future_then(), not flux_future_get() which would deadlock.
 *
 * This function may be called more than once, e.g. to allow SIGKILL to be
 * sent after a timeout of the initial call. In this case the previous
 * future will no longer be fulfilled and should be destroyed.
 *
 * The caller owns the returned future and should eventually destroy it.
 */
flux_future_t *subprocess_server_shutdown (subprocess_server_t *s, int signum);

#endif /* !_SUBPROCESS_SERVER_H */

// vi: ts=4 sw=4 expandtab
