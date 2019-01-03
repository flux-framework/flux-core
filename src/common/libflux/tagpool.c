/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* tagpool.c - allocator for 32-bit matchtags */

/* Matchtags are used to match requests and responses in RPC's.
 * The lower 20 bits are a "tag"; the upper 12 bits are a "group".
 *
 * Requests that receive no response use FLUX_MATCHTAG_NONE (0).
 * Requests that receive one response use a tag.
 * Requests that receive multiple responses use a group.
 *
 * If the group is nonzero, only the group bits are relevant for matching,
 * and the tag bits can be appropriated for user-defined data.
 * For example, flux_rpc_multi() stores the nodeid.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "tagpool.h"
#include "message.h"

#include "src/common/libutil/veb.h"
#include "src/common/libutil/log.h"

#define TAGPOOL_COUNT_REGULAR (1UL<<20)
#define TAGPOOL_COUNT_GROUP (1UL<<12)
#define TAGPOOL_START (1UL<<10)

#define TAGPOOL_MAGIC   0x34447ff2
struct tagpool {
    int             magic;
    Veb             R;
    int             reg_avail;
    Veb             G;
    int             group_avail;
    tagpool_grow_f  grow_cb;
    void            *grow_arg;
    int             grow_depth;
};

static void pool_set (Veb veb, uint32_t from, uint32_t to, uint8_t value)
{
    while (from < to) {
        if (value)
            vebput (veb, from);
        else
            vebdel (veb, from);
        from++;
    }
}

struct tagpool *tagpool_create (void)
{
    struct tagpool *t = calloc (1, sizeof (*t));
    if (!t)
        goto nomem;
    t->magic = TAGPOOL_MAGIC;
    t->R = vebnew (TAGPOOL_START, 1);
    t->G = vebnew (TAGPOOL_START, 1);
    if (!t->R.D || !t->G.D)
        goto nomem;
    vebdel (t->R, FLUX_MATCHTAG_NONE); /* allocate reserved value */
    vebdel (t->G, 0); /* zero group bits means regular tag */
    t->reg_avail = TAGPOOL_COUNT_REGULAR - 1;
    t->group_avail = TAGPOOL_COUNT_GROUP - 1;
    return t;
nomem:
    tagpool_destroy (t);
    errno = ENOMEM;
    return NULL;
}

void tagpool_destroy (struct tagpool *t)
{
    if (t) {
        assert (t->magic == TAGPOOL_MAGIC);
        if (t->R.D)
            free (t->R.D);
        if (t->G.D)
            free (t->G.D);
        t->magic = ~TAGPOOL_MAGIC;
        free (t);
    }
}

void tagpool_set_grow_cb (struct tagpool *t, tagpool_grow_f cb, void *arg)
{
    t->grow_cb = cb;
    t->grow_arg = arg;
}

static uint32_t alloc_with_resize (struct tagpool *t, int flags)
{
    Veb *veb = &t->R;
    uint32_t max = TAGPOOL_COUNT_REGULAR;
    uint32_t oldsize, newsize, tag;

    if ((flags & TAGPOOL_FLAG_GROUP)) {
        veb = &t->G;
        max = TAGPOOL_COUNT_GROUP;
    }
    oldsize = veb->M;
    newsize = oldsize << 1;
    tag = vebsucc (*veb, 0);

    if (tag == veb->M && newsize <= max) {
        if (t->grow_cb && t->grow_depth == 0) {
            t->grow_depth++;
            t->grow_cb (t->grow_arg, oldsize, newsize, flags);
            t->grow_depth--;
        }
        Veb new = vebnew (newsize, 0);
        if (new.D) {
            pool_set (new, oldsize, newsize, 1);
            free (veb->D);
            *veb = new;
            tag = vebsucc (*veb, oldsize);
            assert (tag == oldsize);
        }
    }
    if (tag < veb->M)
        vebdel (*veb, tag);
    return tag;
}

uint32_t tagpool_alloc (struct tagpool *t, int flags)
{
    assert (t->magic == TAGPOOL_MAGIC);
    uint32_t tag;

    if ((flags & TAGPOOL_FLAG_GROUP)) {
        tag = alloc_with_resize (t, TAGPOOL_FLAG_GROUP);
        if (tag < t->G.M) {
            t->group_avail--;
            return tag<<FLUX_MATCHTAG_GROUP_SHIFT;
        }
    } else {
        tag = alloc_with_resize (t, 0);
        if (tag < t->R.M) {
            t->reg_avail--;
            return tag;
        }
    }
    return FLUX_MATCHTAG_NONE;
}

void tagpool_free (struct tagpool *t, uint32_t tag)
{
    assert (t->magic == TAGPOOL_MAGIC);
    if (tag != FLUX_MATCHTAG_NONE) {
        uint32_t group = tag>>FLUX_MATCHTAG_GROUP_SHIFT;
        if (group > 0) {
            if (group < t->G.M) {
                vebput (t->G, group);
                t->group_avail++;
            }
        } else {
            if (tag < t->R.M) {
                vebput (t->R, tag);
                t->reg_avail++;
            }
        }
    }
}

uint32_t tagpool_getattr (struct tagpool *t, int attr)
{
    assert (t->magic == TAGPOOL_MAGIC);
    switch (attr) {
        case TAGPOOL_ATTR_REGULAR_SIZE:
            return TAGPOOL_COUNT_REGULAR - 1;
        case TAGPOOL_ATTR_REGULAR_AVAIL:
            return t->reg_avail;
        case TAGPOOL_ATTR_GROUP_SIZE:
            return TAGPOOL_COUNT_GROUP - 1;
        case TAGPOOL_ATTR_GROUP_AVAIL:
            return t->group_avail;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
