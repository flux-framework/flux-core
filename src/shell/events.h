/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_EVENTS_H
#define _SHELL_EVENTS_H

struct shell_eventlogger;

void shell_eventlogger_destroy (struct shell_eventlogger *shev);
struct shell_eventlogger *shell_eventlogger_create (flux_shell_t *shell);

int shell_eventlogger_emit_event (struct shell_eventlogger *shev,
                                  const char *event);

int shell_eventlogger_context_vpack (struct shell_eventlogger *shev,
                                     const char *event,
                                     int flags,
                                     const char *fmt,
                                     va_list ap);
#endif /* !_SHELL_EVENTS_H */

/* vi: ts=4 sw=4 expandtab
 */

