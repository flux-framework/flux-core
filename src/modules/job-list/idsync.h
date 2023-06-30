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

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job_data.h"

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
    flux_job_state_t state;

    flux_future_t *f_lookup;
};

struct idsync_ctx *idsync_ctx_create (flux_t *h);

void idsync_ctx_destroy (struct idsync_ctx *isctx);

void idsync_data_destroy (void *data);

/* lookup id in KVS to check if it is valid, futures will be tracked /
 * managed in lookups list.  Future returned in idsync_data pointer
 * under 'f_lookup'.
 */
struct idsync_data *idsync_check_id_valid (struct idsync_ctx *isctx,
                                           flux_jobid_t id,
                                           const flux_msg_t *msg,
                                           json_t *attrs,
                                           flux_job_state_t state);


/* free / cleanup 'struct idsync_data' after
 * idsync_check_id_valid().  Don't call this if you re-use
 * 'struct idsync_data' with idsync_wait_valid().
 */
void idsync_check_id_valid_cleanup (struct idsync_ctx *isctx,
                                    struct idsync_data *isd);

/* idsync_wait_valid*() add to job id waits hash, waiting for id to be
 * legal in job-list.  idsync_check_waiting_id() will be able to
 * respond to message at later time when job id becomes available.
 */

int idsync_wait_valid (struct idsync_ctx *isctx, struct idsync_data *isd);

int idsync_wait_valid_id (struct idsync_ctx *isctx,
                          flux_jobid_t id,
                          const flux_msg_t *msg,
                          json_t *attrs,
                          flux_job_state_t state);

/* check if 'job' is in waits list, if so respond to original
 * message */
void idsync_check_waiting_id (struct idsync_ctx *isctx, struct job *job);

#endif /* ! _FLUX_JOB_LIST_IDSYNC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
