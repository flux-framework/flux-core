/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_KILL_H
#define SHELL_KILL_H

#include "shell.h"

/*  Initialize shell kill event handler
 */
int kill_event_init (flux_shell_t *shell);

#endif /* !SHELL_KILL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
