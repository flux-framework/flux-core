/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job_hash.h"

/* Hash numerical jobid in 'key'.
 * N.B. zhashx_hash_fn signature
 */
static size_t job_hasher (const void *key)
{
    const flux_jobid_t *id = key;
    return *id;
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Compare hash keys.
 * N.B. zhashx_comparator_fn signature
 */
static int job_hash_key_cmp (const void *key1, const void *key2)
{
    const flux_jobid_t *id1 = key1;
    const flux_jobid_t *id2 = key2;

    return NUMCMP (*id1, *id2);
}

zhashx_t *job_hash_create (void)
{
    zhashx_t *hash;

    if (!(hash = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_key_hasher (hash, job_hasher);
    zhashx_set_key_comparator (hash, job_hash_key_cmp);
    zhashx_set_key_duplicator (hash, NULL);
    zhashx_set_key_destructor (hash, NULL);

    return hash;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
