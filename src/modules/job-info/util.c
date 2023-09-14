/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>

flux_msg_t *cred_msg_pack (const char *topic,
                           struct flux_msg_cred cred,
                           const char *fmt,
                           ...)
{
    flux_msg_t *newmsg = NULL;
    json_t *payload = NULL;
    char *payloadstr = NULL;
    flux_msg_t *rv = NULL;
    int save_errno;
    va_list ap;

    va_start (ap, fmt);

    if (!(newmsg = flux_request_encode (topic, NULL)))
        goto error;
    if (flux_msg_set_cred (newmsg, cred) < 0)
        goto error;
    if (!(payload = json_vpack_ex (NULL, 0, fmt, ap)))
        goto error;
    if (!(payloadstr = json_dumps (payload, JSON_COMPACT))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_set_string (newmsg, payloadstr) < 0)
        goto error;

    rv = newmsg;
error:
    save_errno = errno;
    if (!rv)
        flux_msg_destroy (newmsg);
    json_decref (payload);
    free (payloadstr);
    va_end (ap);
    errno = save_errno;
    return rv;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
