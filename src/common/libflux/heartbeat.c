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
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "event.h"
#include "message.h"


flux_msg_t *flux_heartbeat_encode (int epoch)
{
    flux_msg_t *msg = NULL;
    char *json_str = NULL;
    json_t *o = json_pack ("{s:i}", "epoch", epoch);

    if (!o || !(json_str = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        goto done;
    }
    if (!(msg = flux_event_encode ("hb", json_str)))
        goto done;
done:
    if (json_str)
        free (json_str);
    json_decref (o);
    return msg;
}

int flux_heartbeat_decode (const flux_msg_t *msg, int *epoch)
{
    json_error_t error;
    json_t *out = NULL;
    const char *json_str, *topic_str;
    int rc = -1;

    if (flux_event_decode (msg, &topic_str, &json_str) < 0)
        goto done;
    if (strcmp (topic_str, "hb") != 0
            || !(out = json_loads (json_str, 0, &error))
            || json_unpack (out, "{s:i}", "epoch", epoch) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    json_decref (out);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
