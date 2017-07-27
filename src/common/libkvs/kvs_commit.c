/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "jansson_dirent.h"

#include "kvs_txn_private.h"
#include "src/common/libutil/blobref.h"

flux_future_t *flux_kvs_fence (flux_t *h, int flags, const char *name,
                               int nprocs, flux_kvs_txn_t *txn)
{
    if (txn) {
        json_t *ops;
        if (txn_get (txn, TXN_GET_ALL, &ops) < 0)
            return NULL;
        return flux_rpc_pack (h, "kvs.fence", FLUX_NODEID_ANY, 0,
                                 "{s:s s:i s:i s:O}",
                                 "name", name,
                                 "nprocs", nprocs,
                                 "flags", flags,
                                 "ops", ops);
    } else {
        return flux_rpc_pack (h, "kvs.fence", FLUX_NODEID_ANY, 0,
                                 "{s:s s:i s:i s:[]}",
                                 "name", name,
                                 "nprocs", nprocs,
                                 "flags", flags,
                                 "ops");
    }
}

flux_future_t *flux_kvs_commit (flux_t *h, int flags, flux_kvs_txn_t *txn)
{
    zuuid_t *uuid;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(uuid = zuuid_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (!(f = flux_kvs_fence (h, flags, zuuid_str (uuid), 1, txn)))
        goto done;
done:
    saved_errno = errno;
    zuuid_destroy (&uuid);
    errno = saved_errno;
    return f;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
