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

#include "response.h"
#include "message.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"


int flux_response_decode (flux_msg_t *msg, const char **topic,
                          const char **json_str)
{
    int type;
    const char *ts, *js;
    int errnum = 0;
    int rc = -1;

    if (msg == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_RESPONSE) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_errnum (msg, &errnum) < 0)
        goto done;
    if (errnum != 0) {
        errno = errnum;
        goto done;
    }
    if (flux_msg_get_topic (msg, &ts) < 0)
        goto done;
    if (flux_msg_get_payload_json (msg, &js) < 0)
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

flux_msg_t *flux_response_encode (const char *topic, int errnum,
                                  const char *json_str)
{
    flux_msg_t *msg = NULL;

    if (!topic || (errnum != 0 && json_str != NULL)) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)))
        goto error;
    if (flux_msg_set_topic (msg, topic) < 0)
        goto error;
    if (flux_msg_enable_route (msg) < 0)
        goto error;
    if (flux_msg_set_errnum (msg, errnum) < 0)
        goto error;
    if (json_str && flux_msg_set_payload_json (msg, json_str) < 0)
        goto error;
    return msg;
error:
    if (msg) {
        int saved_errno = errno;
        flux_msg_destroy (msg);
        errno = saved_errno;
    }
    return NULL;
}

int flux_respond (flux_t h, const flux_msg_t *request,
                  int errnum, const char *json_str)
{
    flux_msg_t *msg = NULL;

    if (!request) {
        errno = EINVAL;
        goto fatal;
    }
    if (!(msg = flux_msg_copy (request, false)))
        goto fatal;
    if (flux_msg_set_type (msg, FLUX_MSGTYPE_RESPONSE) < 0)
        goto fatal;
    if (errnum && flux_msg_set_errnum (msg, errnum) < 0)
        goto fatal;
    if (!errnum && json_str && flux_msg_set_payload_json (msg, json_str) < 0)
        goto fatal;
    if (flux_send (h, msg, 0) < 0)
        goto fatal;
    flux_msg_destroy (msg);
    return 0;
fatal:
    if (msg)
        flux_msg_destroy (msg);
    FLUX_FATAL (h);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
