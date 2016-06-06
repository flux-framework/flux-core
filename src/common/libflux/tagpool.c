/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

#define TAGPOOL_COUNT_REGULAR (1UL<<16) /* consumes about 9K for 1<<16 */
#define TAGPOOL_COUNT_GROUP (1UL<<12)

#define TAGPOOL_MAGIC   0x34447ff2
struct tagpool {
    int             magic;
    Veb             R;
    int             reg_avail;
    Veb             G;
    int             group_avail;
};

struct tagpool *tagpool_create (void)
{
    struct tagpool *t = malloc (sizeof (*t));
    if (!t)
        goto nomem;
    memset (t, 0, sizeof (*t));
    t->magic = TAGPOOL_MAGIC;
    t->R = vebnew (TAGPOOL_COUNT_REGULAR, 1);
    t->G = vebnew (TAGPOOL_COUNT_GROUP, 1);
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

uint32_t tagpool_alloc (struct tagpool *t, int flags)
{
    assert (t->magic == TAGPOOL_MAGIC);
    uint32_t tag;

    if ((flags & TAGPOOL_FLAG_GROUP)) {
       if ((tag = vebsucc (t->G, 0)) != t->G.M) {
            vebdel (t->G, tag);
            t->group_avail--;
            return tag<<FLUX_MATCHTAG_GROUP_SHIFT;
       }
    } else {
       if ((tag = vebsucc (t->R, 0)) != t->R.M) {
            vebdel (t->R, tag);
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
        if ((tag>>FLUX_MATCHTAG_GROUP_SHIFT) > 0) {
            vebput (t->G, tag>>FLUX_MATCHTAG_GROUP_SHIFT);
            t->group_avail++;
        } else {
            vebput (t->R, tag);
            t->reg_avail++;
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

/* Possible future enhancement:  tagpool_setattr for resizing pool(s).
 */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
