/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>

#include "event.h"
#include "message.h"


flux_msg_t *flux_heartbeat_encode (int epoch)
{
    flux_msg_t *msg = NULL;

    if (!(msg = flux_event_pack ("hb", "{ s:i }", "epoch", epoch)))
        return NULL;
    return msg;
}

int flux_heartbeat_decode (const flux_msg_t *msg, int *epoch)
{
    const char *topic_str;
    int rc = -1;

    if (flux_event_unpack (msg, &topic_str, "{ s:i }", "epoch", epoch) < 0)
        goto done;
    if (strcmp (topic_str, "hb") != 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
