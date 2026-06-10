/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  Shared internal functions for shell input plugins
 */
#ifndef SHELL_INPUT_INTERNAL_H
#define SHELL_INPUT_INTERNAL_H

#include <jansson.h>
#include <flux/shell.h>

/*  Initialize the KVS input eventlog. This is done synchronously so that
 *  the eventlog is ready to use after a successful return of this call.
 */
int input_eventlog_init (flux_shell_t *shell);

/*  Put an input eventlog entry `name` defined in `context` to the KVS input
 *  eventlog. Returns 0 on success, -1 on error.
 *  Note: If stdin data exceeds the configured limit (default 10M), this
 *  function will call shell_die() with a fatal error.
 */
int input_eventlog_put_event (flux_shell_t *shell,
                              const char *name,
                              json_t *context);

/*  Flush any pending batched input eventlog entries to KVS immediately.
 *  This should be called when EOF is sent to ensure prompt delivery.
 */
void input_eventlog_flush (flux_shell_t *shell);

/*  Clear all pending input completion references during a reconnect.
 *  During a reconnect, responses to event logging may not occur, so
 *  completion references for inflight transactions must be cleared.
 */
void input_eventlog_reconnect (flux_shell_t *shell);

#endif /* !SHELL_INPUT_INTERNAL_H */

