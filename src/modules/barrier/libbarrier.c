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

#include "src/common/libutil/jsonutil.h"

int flux_barrier (flux_t h, const char *name, int nprocs)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    util_json_object_add_string (request, "name", name);
    util_json_object_add_int (request, "count", 1);
    util_json_object_add_int (request, "nprocs", nprocs);

    reply = flux_rpc (h, request, "barrier.enter");
    if (!reply && errno > 0)
        goto done;
    if (reply) {
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
