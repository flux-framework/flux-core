/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "free.h"

int schedutil_free_request_decode (const flux_msg_t *msg, flux_jobid_t *id)
{
    return flux_request_unpack (msg, NULL, "{s:I}", "id", id);
}

int schedutil_free_respond (flux_t *h, const flux_msg_t *msg)
{
    flux_jobid_t id;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return -1;
    return flux_respond_pack (h, msg, "{s:I}", "id", id);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
