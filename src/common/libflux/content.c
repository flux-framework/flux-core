/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <flux/core.h>

#include "content.h"

#include "src/common/libutil/blobref.h"

flux_future_t *flux_content_load (flux_t *h, const char *blobref, int flags)
{
    const char *topic = "content.load";
    uint32_t rank = FLUX_NODEID_ANY;

    if (!h || !blobref || blobref_validate (blobref) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & CONTENT_FLAG_UPSTREAM))
        rank = FLUX_NODEID_UPSTREAM;
    if ((flags & CONTENT_FLAG_CACHE_BYPASS)) {
        topic = "content-backing.load";
        rank = 0;
    }
    return flux_rpc_raw (h, topic, blobref, strlen (blobref) + 1, rank, 0);
}

int flux_content_load_get (flux_future_t *f, void *buf, int *len)
{
    return flux_rpc_get_raw (f, buf, len);
}

flux_future_t *flux_content_store (flux_t *h, const void *buf, int len, int flags)
{
    const char *topic = "content.store";
    uint32_t rank = FLUX_NODEID_ANY;

    if ((flags & CONTENT_FLAG_UPSTREAM))
        rank = FLUX_NODEID_UPSTREAM;
    if ((flags & CONTENT_FLAG_CACHE_BYPASS)) {
        topic = "content-backing.store";
        rank = 0;
    }
    return flux_rpc_raw (h, topic, buf, len, rank, 0);
}

int flux_content_store_get (flux_future_t *f, const char **blobref)
{
    int ref_size;
    const char *ref;

    if (flux_rpc_get_raw (f, &ref, &ref_size) < 0)
        return -1;
    if (!ref || ref[ref_size - 1] != '\0' || blobref_validate (ref) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (blobref)
        *blobref = ref;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
