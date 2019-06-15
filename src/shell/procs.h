/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_PROCS_H
#define _SHELL_PROCS_H

#include <flux/core.h>
#include "info.h"

struct shell_procs;

struct shell_procs *shell_procs_create (flux_t *h, struct shell_info *info);

void shell_procs_destroy (struct shell_procs *procs);

#endif /* !_SHELL_PROCS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
