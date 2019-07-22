/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_SVC_H
#define SHELL_SVC_H

#include <flux/core.h>

#include "info.h"
#include "task.h"
#include "shell.h"

struct shell_svc;

void shell_svc_destroy (struct shell_svc *svc);
struct shell_svc *shell_svc_create (flux_shell_t *shell);

/* Send an RPC to a shell 'method' by shell rank.
 */
flux_future_t *shell_svc_pack (struct shell_svc *svc,
                               const char *method,
                               int shell_rank,
                               int flags,
                               const char *fmt, ...);


/* Register a message handler for 'method'.
 * The message handler is destroyed when shell->h is destroyed.
 */
int shell_svc_register (struct shell_svc *svc,
                        const char *method,
                        flux_msg_handler_f cb,
                        void *arg);

/* Return 0 if request 'msg' was made by the shell user,
 * else -1 with errno set.
 */
int shell_svc_allowed (struct shell_svc *svc, const flux_msg_t *msg);

#endif /* !SHELL_SVC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
