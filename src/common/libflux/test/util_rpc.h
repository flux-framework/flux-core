/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>

/* Wait for matchtag reclaim logic in dispatcher to reclaim 'count'
 * orphaned matchtags, as responses with no handlers are received.
 * If 'timeout' elapses before that happens, return -1, else return 0.
 */
int reclaim_matchtag (flux_t *h, int count, double timeout);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
