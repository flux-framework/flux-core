/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* kill event handling
 *
 * Handle 'shell-<id>.kill' events by forwarding signal to local tasks
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#include "kill.h"
#include "task.h"
#include "info.h"
#include "shell.h"

static void kill_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    flux_shell_t *shell = arg;
    int signum;
    if (flux_msg_unpack (msg, "{s:i}", "signum", &signum) < 0) {
        log_msg ("kill: ignoring malformed event");
        return;
    }
    flux_shell_killall (shell, signum);
}

int kill_event_init (flux_shell_t *shell)
{
    /* Nothing to do in standalone mode */
    if (shell->standalone)
        return 0;
    if (flux_shell_add_event_handler (shell, "kill", kill_cb, shell) < 0)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
