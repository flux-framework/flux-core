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
#include "response.h"
#include "message.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"


int flux_response_decode (zmsg_t *zmsg, const char **topic,
                          const char **json_str)
{
    int type;
    const char *ts, *js;
    int errnum = 0;
    int rc = -1;

    if (zmsg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_RESPONSE) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_errnum (zmsg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    if (flux_msg_get_topic (zmsg, &ts) < 0)
        goto done;
    if (flux_msg_get_payload_json (zmsg, &js) < 0)
        goto done;
    if ((json_str && !js) || (!json_str && js)) {
        errno = EPROTO;
        goto done;
    }
    if (topic)
        *topic = ts;
    if (json_str)
        *json_str = js;
    rc = 0;
done:
    return rc;
}

zmsg_t *flux_response_encode (const char *topic, int errnum,
                              const char *json_str)
{
    zmsg_t *zmsg = NULL;

    if (!topic || (errnum != 0 && json_str != NULL)) {
        errno = EINVAL;
        goto error;
    }
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)))
        goto error;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto error;
    if (flux_msg_enable_route (zmsg) < 0)
        goto error;
    if (flux_msg_set_errnum (zmsg, errnum) < 0)
        goto error;
    if (json_str && flux_msg_set_payload_json (zmsg, json_str) < 0)
        goto error;
    return zmsg;
error:
    if (zmsg) {
        int saved_errno = errno;
        zmsg_destroy (&zmsg);
        errno = saved_errno;
    }
    return NULL;
}

zmsg_t *flux_response_encode_ok (zmsg_t *request, const char *json_str)
{
    zmsg_t *zmsg = NULL;

    if (!request) {
        errno = EINVAL;
        goto error;
    }
    if (!(zmsg = zmsg_dup (request))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_set_type (zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto error;
    if (flux_msg_set_errnum (zmsg, 0) < 0)
        goto error;
    if (flux_msg_set_payload_json (zmsg, json_str) < 0)
        goto error;
    return zmsg;
error:
    if (zmsg) {
        int saved_errno = errno;
        zmsg_destroy (&zmsg);
        errno = saved_errno;
    }
    return NULL;
}

zmsg_t *flux_response_encode_err (zmsg_t *request, int errnum)
{
    zmsg_t *zmsg = NULL;

    if (!request) {
        errno = EINVAL;
        goto error;
    }
    if (!(zmsg = zmsg_dup (request))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_set_type (zmsg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto error;
    if (flux_msg_set_errnum (zmsg, errnum) < 0)
        goto error;
    if (flux_msg_set_payload_json (zmsg, NULL) < 0)
        goto error;
    return zmsg;
error:
    if (zmsg) {
        int saved_errno = errno;
        zmsg_destroy (&zmsg);
        errno = saved_errno;
    }
    return NULL;
}

int flux_response_send (flux_t h, zmsg_t **zmsg)
{
    return flux_sendmsg (h, zmsg);
}

zmsg_t *flux_response_recv (flux_t h, uint32_t matchtag, bool nonblock)
{
    flux_match_t match = {
        .typemask = FLUX_MSGTYPE_RESPONSE,
        .matchtag = matchtag,
        .bsize = 0,
        .topic_glob = NULL,
    };
    return flux_recvmsg_match (h, match, NULL, nonblock);

}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
