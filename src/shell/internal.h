/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_INTERNAL_H
#define _SHELL_INTERNAL_H

#include <czmq.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/shell.h>

#include "src/common/libutil/aux.h"

struct flux_shell {
    flux_jobid_t jobid;
    int broker_rank;

    optparse_t *p;
    flux_t *h;
    flux_reactor_t *r;

    struct shell_info *info;
    struct shell_svc *svc;
    struct shell_io *io;
    struct shell_pmi *pmi;
    zlist_t *tasks;
    flux_shell_task_t *current_task;

    zhashx_t *completion_refs;

    int rc;

    bool verbose;
    bool standalone;

    struct aux_item *aux;
};

#endif /* !_SHELL_INTERNAL_H */

