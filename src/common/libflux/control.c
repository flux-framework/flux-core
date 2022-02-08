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

#include "control.h"

flux_msg_t *flux_control_encode (int type, int status)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_CONTROL)))
        goto error;
    if (flux_msg_set_control (msg, type, status) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

int flux_control_decode (const flux_msg_t *msg, int *typep, int *statusp)
{
    int type, status;

    if (flux_msg_get_control (msg, &type, &status) < 0)
        return -1;
    if (typep)
        *typep = type;
    if (statusp)
        *statusp = status;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
