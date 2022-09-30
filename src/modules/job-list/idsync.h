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

#include "job-list.h"

struct idsync_data {
    struct list_ctx *ctx;
    flux_jobid_t id;
    flux_msg_t *msg;
    json_t *attrs;

    flux_future_t *f_lookup;
};

int idsync_setup (struct list_ctx *ctx);

void idsync_cleanup (struct list_ctx *ctx);

void idsync_data_destroy (void *data);

void idsync_data_destroy_wrapper (void **data);

struct idsync_data *idsync_data_create (struct list_ctx *ctx,
                                        flux_jobid_t id,
                                        const flux_msg_t *msg,
                                        json_t *attrs,
                                        flux_future_t *f_lookup);

#endif /* ! _FLUX_JOB_LIST_IDSYNC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
