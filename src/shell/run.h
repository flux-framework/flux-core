/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_RUN_H
#define _SHELL_RUN_H

#include <flux/core.h>
#include "info.h"

int shell_run (flux_t *h, struct shell_info *info);

#endif /* !_SHELL_RUN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
