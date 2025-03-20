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

/* N.B. zlistx_t is predominantly use for storage b/c we need to
 * iterate and remove entries while iterating.  This cannot be done
 * with a zhashx_t unless we use the relatively costly zhashx_keys().
 * So a number of lists are supplemented with a hash for when a
 * quick lookup would be advantageous.
 */
struct info_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    lru_cache_t *owner_lru; /* jobid -> owner LRU */
    zlistx_t *lookups;
    zlistx_t *watchers;
    zhashx_t *watchers_matchtags; /* matchtag + uuid -> watcher */
    zlistx_t *guest_watchers;
    zhashx_t *guest_watchers_matchtags; /* matchtag + uuid -> guest_watcher */
    zlistx_t *update_watchers;
    zhashx_t *index_uw;        /* jobid + key -> update_watcher lookup */
};

#endif /* _FLUX_JOB_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

