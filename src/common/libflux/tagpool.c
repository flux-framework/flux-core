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
 *
 * Requests that receive no response use FLUX_MATCHTAG_NONE (0).
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

#define TAGPOOL_COUNT (1UL<<20)
#define TAGPOOL_START (1UL<<10)

#define TAGPOOL_MAGIC   0x34447ff2
struct tagpool {
    int             magic;
    Veb             veb;
    int             avail;
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
    t->veb = vebnew (TAGPOOL_START, 1);
    if (!t->veb.D)
        goto nomem;
    vebdel (t->veb, FLUX_MATCHTAG_NONE); /* allocate reserved value */
    t->avail = TAGPOOL_COUNT - 1;
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
        free (t->veb.D);
        t->magic = ~TAGPOOL_MAGIC;
        free (t);
    }
}

void tagpool_set_grow_cb (struct tagpool *t, tagpool_grow_f cb, void *arg)
{
    t->grow_cb = cb;
    t->grow_arg = arg;
}

static uint32_t alloc_with_resize (struct tagpool *t)
{
    uint32_t max = TAGPOOL_COUNT;
    uint32_t oldsize, newsize, tag;

    oldsize = t->veb.M;
    newsize = oldsize << 1;
    tag = vebsucc (t->veb, 0);

    if (tag == t->veb.M && newsize <= max) {
        if (t->grow_cb && t->grow_depth == 0) {
            t->grow_depth++;
            t->grow_cb (t->grow_arg, oldsize, newsize);
            t->grow_depth--;
        }
        Veb new = vebnew (newsize, 0);
        if (new.D) {
            pool_set (new, oldsize, newsize, 1);
            free (t->veb.D);
            t->veb = new;
            tag = vebsucc (t->veb, oldsize);
            assert (tag == oldsize);
        }
    }
    if (tag < t->veb.M)
        vebdel (t->veb, tag);
    return tag;
}

uint32_t tagpool_alloc (struct tagpool *t)
{
    assert (t->magic == TAGPOOL_MAGIC);
    uint32_t tag;

    tag = alloc_with_resize (t);
    if (tag < t->veb.M) {
        t->avail--;
        return tag;
    }
    return FLUX_MATCHTAG_NONE;
}

void tagpool_free (struct tagpool *t, uint32_t tag)
{
    assert (t->magic == TAGPOOL_MAGIC);
    if (tag != FLUX_MATCHTAG_NONE) {
        if (tag < t->veb.M) {
            vebput (t->veb, tag);
            t->avail++;
        }
    }
}

uint32_t tagpool_getattr (struct tagpool *t, int attr)
{
    assert (t->magic == TAGPOOL_MAGIC);
    switch (attr) {
        case TAGPOOL_ATTR_SIZE:
            return TAGPOOL_COUNT - 1;
        case TAGPOOL_ATTR_AVAIL:
            return t->avail;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
