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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>

#include "src/common/libflux/rpc.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "compat.h"

int flux_json_rpc (flux_t *h, uint32_t nodeid, const char *topic,
                   json_object *in, json_object **out)
{
    flux_rpc_t *rpc;
    const char *json_str;
    json_object *o = NULL;
    int rc = -1;

    if (!(rpc = flux_rpc (h, topic, Jtostr (in), nodeid, 0)))
        goto done;
    if (flux_rpc_get (rpc, &json_str) < 0)
        goto done;
    if (out) {
        if (!json_str || !(o = Jfromstr (json_str))) {
            errno = EPROTO;
            goto done;
        }
        *out = o;
    }
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
