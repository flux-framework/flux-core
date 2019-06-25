/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_PMI_H
#define SHELL_PMI_H

#include <flux/core.h>
#include <czmq.h>

#include "info.h"
#include "task.h"

struct shell_pmi;

void shell_pmi_destroy (struct shell_pmi *pmi);
struct shell_pmi *shell_pmi_create (flux_t *h, struct shell_info *info);

// shell_task_pmi_ready_f callback footprint
void shell_pmi_task_ready (struct shell_task *task, void *arg);


#endif /* !SHELL_PMI_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
