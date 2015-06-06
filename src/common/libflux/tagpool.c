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
 * There are two main use cases in rpc.c.  The plain rpc call allocates
 * and retires one matchtag, the multiple call allocates a block of
 * matchtags.  kvs_watch() is another use case.  It sends one request
 * and reecives multiple replies with the same matchtag.
 *
 * This implementation could be improved:
 * - allocations of len = 1 are allocated from a fixed 2^16 tag pool,
 *   which perhaps should be dynamically resized up to 2^24
 * - allocations of len > 2 always consume a full 2^24 tag block,
 *   and there are only 255 blocks available
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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/veb.h"
#include "src/common/libutil/log.h"

#define TAGPOOL_BSIZE   (1UL<<24)
#define TAGPOOL_LENGTH  (1UL<<8)

#define TAGPOOL_VEBSIZE (1UL<<16)    /* uses 9K for 1<<16 */

#define TAGPOOL_MAGIC   0x34447ff2
struct tagpool_struct {
    int             magic;
    int             blocks[TAGPOOL_LENGTH];
    Veb             T;
    uint32_t        count;
};

tagpool_t tagpool_create (void)
{
    tagpool_t t = xzmalloc (sizeof (*t));
    t->magic = TAGPOOL_MAGIC;
    t->T = vebnew (TAGPOOL_VEBSIZE, 1); /* FIXME make dynamic? */
    if (!t->T.D)
        oom ();
    vebdel (t->T, FLUX_MATCHTAG_NONE); /* don't allocate that one! */
    return t;
}

void tagpool_destroy (tagpool_t t)
{
    if (t) {
        assert (t->magic == TAGPOOL_MAGIC);
        if (t->T.D)
            free (t->T.D);
        t->magic = ~TAGPOOL_MAGIC;
        free (t);
    }
}

/* If asking for one tag, allocate from the veb pool convering block 0.
 * If asking for >1, grab a whole block of 1<<24.
 */
uint32_t tagpool_alloc (tagpool_t t, int len)
{
    assert (t->magic == TAGPOOL_MAGIC);
    uint32_t matchtag = FLUX_MATCHTAG_NONE;
    if (len == 1) {
        uint32_t tag = vebsucc (t->T, 0); /* first from free set */
        if (tag != t->T.M) {
            vebdel (t->T, tag);
            matchtag = tag;
            t->count++;
        }
    } else if (len > 1 && len < TAGPOOL_BSIZE) {
        int i;
        for (i = 1; i < TAGPOOL_LENGTH; i++) {
            if (t->blocks[i] == 0) {
                t->blocks[i] = len;
                matchtag = ((uint32_t)i)<<24;
                t->count += TAGPOOL_BSIZE;
                break;
            }
        }
    }
    return matchtag;
}

/* If freeing one tag, add back to the veb pool.
 * If freeing >1, free block of 1<<24 (but len must match).
 */
void tagpool_free (tagpool_t t, uint32_t matchtag, int len)
{
    assert (t->magic == TAGPOOL_MAGIC);
    if (matchtag != FLUX_MATCHTAG_NONE) {
        if (len == 1) {
            vebput (t->T, matchtag); /* return to free set */
            t->count--;
        } else {
            int i = matchtag>>24;
            if (i < TAGPOOL_LENGTH && t->blocks[i] == len) {
                t->blocks[i] = 0;
                t->count -= TAGPOOL_BSIZE;
            }
        }
    }
}

uint32_t tagpool_avail (tagpool_t t)
{
    assert (t->magic == TAGPOOL_MAGIC);
    const uint32_t total = TAGPOOL_BSIZE * (TAGPOOL_LENGTH - 1)
                         + TAGPOOL_VEBSIZE - 1;
    return total - t->count;
}

uint32_t tagpool_getattr (tagpool_t t, int attr)
{
    assert (t->magic == TAGPOOL_MAGIC);
    switch (attr) {
        case TAGPOOL_ATTR_BLOCKS:
            return TAGPOOL_LENGTH - 1;
        case TAGPOOL_ATTR_BLOCKSIZE:
            return TAGPOOL_BSIZE;
        case TAGPOOL_ATTR_SSIZE:
            return TAGPOOL_VEBSIZE - 1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
