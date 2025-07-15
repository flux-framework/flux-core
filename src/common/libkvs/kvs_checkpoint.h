/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _KVS_CHECKPOINT_H
#define _KVS_CHECKPOINT_H

#include <flux/core.h>

/* flags */
enum {
    KVS_CHECKPOINT_FLAG_CACHE_BYPASS = 1,/* request direct to backing store */
};

#define KVS_DEFAULT_CHECKPOINT "kvs-primary"

/* Calls to kvs_checkpoint_commit() can be racy when the KVS module is
 * loaded, as callers can use the FLUX_KVS_SYNC flag with
 * flux_kvs_commit().
 *
 * In most cases, use the FLUX_KVS_SYNC flag with flux_kvs_commit() to
 * ensure committed data survives a Flux restart.
 */

flux_future_t *kvs_checkpoint_commit (flux_t *h,
                                      const char *rootref,
                                      int sequence,
                                      double timestamp,
                                      int flags);

flux_future_t *kvs_checkpoint_lookup (flux_t *h, int flags);

/* return array of checkpoints */
int kvs_checkpoint_lookup_get (flux_future_t *f, const json_t **checkpoints);

int kvs_checkpoint_parse_rootref (json_t *checkpoint, const char **rootref);

/* sets timestamp to 0 if unavailable
 */
int kvs_checkpoint_parse_timestamp (json_t *checkpoint, double *timestamp);

/* sets sequence to 0 if unavailable
 */
int kvs_checkpoint_parse_sequence (json_t *checkpoint, int *sequence);

#endif /* !_KVS_CHECKPOINT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
