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
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"

int flux_failover (flux_t *h, uint32_t rank)
{
    flux_rpc_t *rpc;
    int rc = -1;

    if (!(rpc = flux_rpc (h, "live.failover", NULL, rank, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

int flux_recover (flux_t *h, uint32_t rank)
{
    flux_rpc_t *rpc;
    int rc = -1;

    if (!(rpc = flux_rpc (h, "live.recover", NULL, rank, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

int flux_recover_all (flux_t *h)
{
    int rc = -1;
    flux_msg_t *msg;

    if (!(msg  = flux_event_encode ("live.recover", NULL)))
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
