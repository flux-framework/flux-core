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

/*  Put an input eventlog entry defined in `context` to the KVS input
 *  eventlog.
 */
int input_eventlog_put (flux_shell_t *shell, json_t *context);

#endif /* !SHELL_INPUT_INTERNAL_H */

