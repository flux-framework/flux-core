/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_TERMINUS_H
#define FLUX_TERMINUS_H

#include <stdarg.h>

#include <flux/core.h>


struct flux_terminus_server;

/*  Logging function prototype */
typedef void (*terminus_log_f) (void *arg,
                                const char *file,
                                int line,
                                const char *func,
                                const char *subsys,
                                int level,
                                const char *fmt,
                                va_list args);

/*  Callback prototype for use when a terminus server becomes "empty",
 *   i.e. the last running session exits. (Used for optional cleanup).
 */
typedef void (*flux_terminus_server_empty_f) (struct flux_terminus_server *ts,
                                              void *arg);


/*  Create a flux_terminus_server listening at topic `service`
 */
struct flux_terminus_server *
flux_terminus_server_create (flux_t *h, const char *service);

void flux_terminus_server_destroy (struct flux_terminus_server *ts);

/*  Set internal libterminus logging function to 'log_fn'. If unset,
 *   then logging from the library is disabled.
 */
void flux_terminus_server_set_log (struct flux_terminus_server *ts,
                                   terminus_log_f log_fn,
                                   void *log_data);

/*  Call function `fn` when terminus server next becomes empty, i.e. goes
 *   from having 1 or more sessions to no sessions. (if there are no
 *   current sessions, then the empty cb will *not* be called immediately,
 *   only after at least one session is created and destroyed)
 *
 *  Callbacks are oneshot, i.e. they are removed after being called.
 */
int flux_terminus_server_notify_empty (struct flux_terminus_server *ts,
                                       flux_terminus_server_empty_f fn,
                                       void *arg);

/*  Open a session directly in the server ts (as oppopsed to via the
 *   protocol.
 */
struct flux_pty *
flux_terminus_server_session_open (struct flux_terminus_server *ts,
                                   int id,
                                   const char *name);

int flux_terminus_server_session_close (struct flux_terminus_server *ts,
                                        struct flux_pty *pty,
                                        int status);

/*  Unregister the terminus server service.
 *  Returns a future that will be fulfilled when the service is successfully
 *  unregistered.
 */
flux_future_t *
flux_terminus_server_unregister (struct flux_terminus_server *ts);

#endif /* !FLUX_TERMINUS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
