/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_LOG_H
#    define BROKER_LOG_H

#    include <flux/core.h>
#    include "attr.h"

int logbuf_initialize (flux_t *h, uint32_t rank, attr_t *attrs);

#endif /* BROKER_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
