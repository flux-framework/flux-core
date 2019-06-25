/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_IO_H
#define SHELL_IO_H

#include <flux/core.h>

#include "info.h"
#include "task.h"

struct shell_io;

void shell_io_destroy (struct shell_io *io);
struct shell_io *shell_io_create (flux_t *h, struct shell_info *info);

// shell_task_io_ready_f callback footprint
void shell_io_task_ready (struct shell_task *task, const char *name, void *arg);

#endif /* !SHELL_IO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
