/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_BROKER_CONTENT_CHECKPOINT_H
#define HAVE_BROKER_CONTENT_CHECKPOINT_H 1

#include "content-cache.h"

struct content_checkpoint *content_checkpoint_create (
    flux_t *h,
    uint32_t rank,
    struct content_cache *cache);
void content_checkpoint_destroy (struct content_checkpoint *checkpoint);

#endif /* !HAVE_BROKER_CONTENT_CHECKPOINT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
