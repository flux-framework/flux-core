/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>
#include <time.h>

#include "kvs_checkpoint.h"

flux_future_t *kvs_checkpoint_commit (flux_t *h,
                                      const char *rootref,
                                      int sequence,
                                      double timestamp,
                                      int flags)
{
    flux_future_t *f = NULL;
    const char *topic = "content.checkpoint-put";
    int valid_flags = KVS_CHECKPOINT_FLAG_CACHE_BYPASS;

    if (!h || !rootref || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (timestamp == 0)
        timestamp = flux_reactor_now (flux_get_reactor (h));
    if (flags & KVS_CHECKPOINT_FLAG_CACHE_BYPASS)
        topic = "content-backing.checkpoint-put";

    if (!(f = flux_rpc_pack (h,
                             topic,
                             0,
                             0,
                             "{s:{s:i s:s s:i s:f}}",
                             "value",
                             "version", 1,
                             "rootref", rootref,
                             "sequence", sequence,
                             "timestamp", timestamp)))
        return NULL;

    return f;
}

flux_future_t *kvs_checkpoint_lookup (flux_t *h, int flags)
{
    const char *topic = "content.checkpoint-get";
    int valid_flags = KVS_CHECKPOINT_FLAG_CACHE_BYPASS;

    if (!h || (flags & ~valid_flags)) {
        errno = EINVAL;
        return NULL;
    }
    if (flags & KVS_CHECKPOINT_FLAG_CACHE_BYPASS)
        topic = "content-backing.checkpoint-get";

    return flux_rpc (h, topic, NULL, 0, 0);
}

int kvs_checkpoint_lookup_get_rootref (flux_future_t *f, const char **rootref)
{
    const char *tmp_rootref;
    int version;

    if (!f || !rootref) {
        errno = EINVAL;
        return -1;
    }

    if (flux_rpc_get_unpack (f,
                             "{s:{s:i s:s}}",
                             "value",
                               "version", &version,
                               "rootref", &tmp_rootref) < 0)
        return -1;

    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }

    (*rootref) = tmp_rootref;
    return 0;
}

int kvs_checkpoint_lookup_get_timestamp (flux_future_t *f, double *timestamp)
{
    int version;
    double ts = 0.;

    if (!f || !timestamp) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:{s:i s?f}}",
                             "value",
                               "version", &version,
                               "timestamp", &ts) < 0)
        return -1;
    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }
    *timestamp = ts;
    return 0;
}

int kvs_checkpoint_lookup_get_sequence (flux_future_t *f, int *sequence)
{
    int version;
    int seq = 0;

    if (!f || !sequence) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f,
                             "{s:{s:i s?i}}",
                             "value",
                               "version", &version,
                               "sequence", &seq) < 0)
        return -1;
    if (version != 0 && version != 1) {
        errno = EINVAL;
        return -1;
    }
    *sequence = seq;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
