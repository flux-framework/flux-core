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

#include "src/common/libflux/message.h"
#include "src/common/libflux/info.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

#include "compat.h"

int flux_json_request (flux_t h, uint32_t nodeid, uint32_t matchtag,
                       const char *topic, JSON in)
{
    zmsg_t *zmsg;
    int rc = -1;
    int flags = 0;

    if (!topic) {
        errno = EINVAL;
        goto done;
    }
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (nodeid == FLUX_NODEID_UPSTREAM) {
        flags |= FLUX_MSGFLAG_UPSTREAM;
        if (flux_get_rank (h, &nodeid) < 0)
            goto done;
    }
    if (flux_msg_set_nodeid (zmsg, nodeid, flags) < 0)
        goto done;
    if (flux_msg_set_matchtag (zmsg, matchtag) < 0)
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (flux_msg_set_payload_json (zmsg, in ? Jtostr (in) : NULL) < 0)
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    rc = flux_send (h, zmsg, 0);
done:
    zmsg_destroy (&zmsg);
    return rc;
}

int flux_json_respond (flux_t h, JSON out, zmsg_t **zmsg)
{
    int rc = -1;

    if (flux_msg_set_type (*zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto done;
    if (flux_msg_set_payload_json (*zmsg, out ? Jtostr (out) : NULL) < 0)
        goto done;
    if (flux_send (h, *zmsg, 0) < 0)
        goto done;
    zmsg_destroy (zmsg);
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
