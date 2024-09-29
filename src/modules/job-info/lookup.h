/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_LOOKUP_H
#define _FLUX_JOB_INFO_LOOKUP_H

#include <flux/core.h>

void lookup_cb (flux_t *h,
                flux_msg_handler_t *mh,
                const flux_msg_t *msg,
                void *arg);

/* legacy rpc target */
void update_lookup_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg);

#endif /* ! _FLUX_JOB_INFO_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
