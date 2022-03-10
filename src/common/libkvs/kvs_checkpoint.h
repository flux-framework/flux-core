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

flux_future_t *kvs_checkpoint_commit (flux_t *h,
                                      const char *key,
                                      const char *rootref,
                                      double timestamp);

flux_future_t *kvs_checkpoint_lookup (flux_t *h, const char *key);

int kvs_checkpoint_lookup_get_rootref (flux_future_t *f, const char **rootref);

/* sets timestamp to 0 if unavailable
 */
int kvs_checkpoint_lookup_get_timestamp (flux_future_t *f, double *timestamp);

#endif /* !_KVS_CHECKPOINT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
