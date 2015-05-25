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
#include "reparent.h"

#include "src/common/libjsonc/jsonc.h"
#include "src/common/libutil/shortjson.h"


JSON flux_lspeer (flux_t h, int rank)
{
    uint32_t nodeid = (rank == -1 ? FLUX_NODEID_ANY : rank);
    JSON out = NULL;

    if (flux_json_rpc (h, nodeid, "cmb.lspeer", NULL, &out) < 0)
        return NULL;
    return out;
}

int flux_reparent (flux_t h, int rank, const char *uri)
{
    uint32_t nodeid = (rank == -1 ? FLUX_NODEID_ANY : rank);
    JSON in = Jnew ();
    int rc = -1;

    if (!uri) {
        errno = EINVAL;
        goto done;
    }
    Jadd_str (in, "uri", uri);
    if (flux_json_rpc (h, nodeid, "cmb.reparent", in, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
