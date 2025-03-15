/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_H
#define _FLUX_JOB_INFO_H

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libutil/lru_cache.h"

#define OWNER_LRU_MAXSIZE 1000

struct info_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    lru_cache_t *owner_lru; /* jobid -> owner LRU */
    zlistx_t *lookups;
    zlistx_t *watchers;
    zlistx_t *guest_watchers;
    zlistx_t *update_watchers;
    zhashx_t *index_uw;        /* jobid + key -> update_watcher lookup */
};

#endif /* _FLUX_JOB_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

