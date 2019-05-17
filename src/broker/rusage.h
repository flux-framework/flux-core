/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_RUSAGE_H
#    define BROKER_RUSAGE_H

#    include <flux/core.h>

int rusage_initialize (flux_t *h, const char *service);

#endif /* BROKER_RUSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
