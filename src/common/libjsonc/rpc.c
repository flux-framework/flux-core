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

#include "src/common/libflux/message.h"
#include "src/common/libflux/info.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"

#include "jsonc.h"

int flux_json_rpc (flux_t h, uint32_t nodeid, const char *topic,
                   JSON in, JSON *out)
{
    zmsg_t *zmsg = NULL;
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .matchtag = flux_matchtag_alloc (h, 1),
        .bsize = 1,
        .topic_glob = NULL,
    };
    int rc = -1;
    int errnum;
    const char *json_str;
    JSON o = NULL;

    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errno = EAGAIN;
        goto done;
    }
    if (flux_json_request (h, nodeid, match.matchtag, topic, in) < 0)
        goto done;
    if (!(zmsg = flux_recvmsg_match (h, match, NULL, false)))
        goto done;
    if (flux_msg_get_errnum (zmsg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    if (flux_msg_get_payload_json (zmsg, &json_str) < 0)
        goto done;
    if (json_str && !(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if ((!o && out)) {
        errno = EPROTO;
        goto done;
    }
    if ((o && !out)) {
        Jput (o);
        errno = EPROTO;
        goto done;
    }
    if (out)
        *out = o;
    rc = 0;
done:
    if (match.matchtag != FLUX_MATCHTAG_NONE)
        flux_matchtag_free (h, match.matchtag, match.bsize);
    zmsg_destroy (&zmsg);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
