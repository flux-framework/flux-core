/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_TAGPOOL_H
#define _FLUX_CORE_TAGPOOL_H

#include <stdint.h>
#include <stdbool.h>

enum {
    TAGPOOL_FLAG_GROUP = 1,
};

struct tagpool *tagpool_create (void);
void tagpool_destroy (struct tagpool *t);
uint32_t tagpool_alloc (struct tagpool *t, int flags);
void tagpool_free (struct tagpool *t, uint32_t matchtag);

bool tagpool_group (uint32_t matchtag);

typedef void (*tagpool_grow_f)(void *arg, uint32_t oldsize, uint32_t newsize, int flags);
void tagpool_set_grow_cb (struct tagpool *t, tagpool_grow_f cb, void *arg);

enum {
    TAGPOOL_ATTR_REGULAR_SIZE,
    TAGPOOL_ATTR_REGULAR_AVAIL,
    TAGPOOL_ATTR_GROUP_SIZE,
    TAGPOOL_ATTR_GROUP_AVAIL,
};
uint32_t tagpool_getattr (struct tagpool *t, int attr);


#endif /* _FLUX_CORE_TAGPOOL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
