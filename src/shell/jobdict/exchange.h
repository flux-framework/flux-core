/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_JOBDICT_EXCHANGE_H
#define SHELL_JOBDICT_EXCHANGE_H

#include <stdbool.h>
#include <jansson.h>
#include <flux/shell.h>

struct exchange;

/* Invoked when the exchange completes (successfully or with error).
 * The callback should check exchange_has_error() and, if no error, may read
 * the aggregate dict with exchange_get_result().
 */
typedef void (*exchange_exit_f)(struct exchange *xcg, void *arg);

/* Shell service method for inter-shell exchange RPCs.  The owner of the
 * exchange handles registers it once and routes each request to the handle
 * named in the request payload with exchange_request().
 */
#define EXCHANGE_SERVICE "jobdict"

/* Create a handle for performing sequential exchanges over all shell ranks.
 * 'name' identifies this handle in inter-shell RPCs and log messages (must be
 * unique within the shell).  'k' is the tree fanout (k=0 selects internal
 * default).
 */
struct exchange *exchange_create (flux_shell_t *shell,
                                  const char *name,
                                  int k);
void exchange_destroy (struct exchange *xcg);

/* Handle an EXCHANGE_SERVICE request from another shell addressed to this
 * handle.  A response is always sent, now or on exchange completion.
 */
void exchange_request (struct exchange *xcg, const flux_msg_t *msg);

/* Enter one exchange with this shell's local 'dict' (a json object, or NULL
 * to contribute nothing).  'exit_cb' is invoked once the aggregate dict is
 * available on this shell.
 */
int exchange_enter (struct exchange *xcg,
                    json_t *dict,
                    exchange_exit_f exit_cb,
                    void *arg);

/* Accessors may be called only from the exchange_exit_f callback.
 * exchange_get_result() returns a json object that is invalidated when the
 * callback returns.
 */
bool exchange_has_error (struct exchange *xcg);
json_t *exchange_get_result (struct exchange *xcg);

#endif /* !SHELL_JOBDICT_EXCHANGE_H */

/* vi: ts=4 sw=4 expandtab
 */
