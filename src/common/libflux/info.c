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
#include <errno.h>
#include <stdbool.h>

#include "info.h"
#include "rpc.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

char *flux_getattr (flux_t h, int rank, const char *name)
{
    uint32_t nodeid = (rank == -1 ? FLUX_NODEID_ANY : rank);
    JSON in = Jnew ();
    JSON out = NULL;
    char *ret = NULL;
    const char *val = NULL;

    Jadd_str (in, "name", name);
    if (flux_json_rpc (h, nodeid, "cmb.getattr", in, &out) < 0)
        goto done;
    if (!out || !Jget_str (out, (char *)name, &val)) {
        errno = EPROTO;
        goto done;
    }
    ret = xstrdup (val);
done:
    Jput (in);
    Jput (out);
    return ret;
}

int flux_info (flux_t h, int *rankp, int *sizep, bool *treerootp)
{
    JSON response = NULL;
    int rank, size;
    bool treeroot;
    int ret = -1;

    if (flux_json_rpc (h, FLUX_NODEID_ANY, "cmb.info", NULL, &response) < 0)
        goto done;
    if (!Jget_bool (response, "treeroot", &treeroot)
            || !Jget_int (response, "rank", &rank)
            || !Jget_int (response, "size", &size)) {
        errno = EPROTO;
        goto done;
    }
    if (rankp)
        *rankp = rank;
    if (sizep)
        *sizep = size;
    if (treerootp)
        *treerootp = treeroot;
    ret = 0;
done:
    Jput (response);
    return ret;
}

int flux_size (flux_t h)
{
    int size = -1;
    flux_info (h, NULL, &size, NULL);
    return size;
}

bool flux_treeroot (flux_t h)
{
    bool treeroot = false;
    flux_info (h, NULL, NULL, &treeroot);
    return treeroot;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
