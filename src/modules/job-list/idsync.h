/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_IDSYNC_H
#define _FLUX_JOB_LIST_IDSYNC_H

#include <flux/core.h>

struct idsync_ctx {
    flux_t *h;
    zlistx_t *lookups;
    zhashx_t *waits;
};

struct idsync_data {
    flux_t *h;
    flux_jobid_t id;
    flux_msg_t *msg;
    json_t *attrs;

    flux_future_t *f_lookup;
};

struct idsync_ctx *idsync_ctx_create (flux_t *h);

void idsync_ctx_destroy (struct idsync_ctx *isctx);

void idsync_data_destroy (void *data);

void idsync_data_destroy_wrapper (void **data);

struct idsync_data *idsync_data_create (flux_t *h,
                                        flux_jobid_t id,
                                        const flux_msg_t *msg,
                                        json_t *attrs,
                                        flux_future_t *f_lookup);

#endif /* ! _FLUX_JOB_LIST_IDSYNC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
