/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_SIGNAL_H
#define SHELL_SIGNAL_H

#include "shell.h"

/*  Initialize shell signal handlers
 */
int signals_init (flux_shell_t *shell);

#endif /* !SHELL_SIGNAL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
