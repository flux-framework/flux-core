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

#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/shell.h>
#include <limits.h>

#include "src/common/libutil/aux.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "plugstack.h"
#include "events.h"
#include "mustache.h"

struct flux_shell {
    flux_jobid_t jobid;
    int broker_rank;
    uid_t broker_owner;
    char hostname [_POSIX_HOST_NAME_MAX + 1];
    int protocol_fd[2];

    optparse_t *p;
    flux_t *h;
    flux_reactor_t *r;

    struct shell_info *info;
    struct shell_svc *svc;
    zlist_t *tasks;
    flux_shell_task_t *current_task;
    struct mustache_renderer *mr;

    struct plugstack *plugstack;
    struct shell_eventlogger *ev;

    zhashx_t *completion_refs;

    int rc;

    int verbose;
    int nosetpgrp;

    struct aux_item *aux;
};

#endif /* !_SHELL_INTERNAL_H */

