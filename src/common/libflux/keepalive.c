/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "keepalive.h"

flux_msg_t *flux_keepalive_encode (int errnum, int status)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)))
        goto error;
    if (flux_msg_set_errnum (msg, errnum) < 0)
        goto error;
    if (flux_msg_set_status (msg, status) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int flux_keepalive_decode (const flux_msg_t *msg, int *ep, int *sp)
{
    int rc = -1;
    int errnum, status;

    if (flux_msg_get_errnum (msg, &errnum) < 0)
        goto done;
    if (flux_msg_get_status (msg, &status) < 0)
        goto done;
    if (ep)
        *ep = errnum;
    if (sp)
        *sp = status;
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
