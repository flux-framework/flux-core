/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZMQUTIL_ZWATCHER_H
#define _ZMQUTIL_ZWATCHER_H

#include <stdio.h>
#include <stdlib.h>
#include <flux/core.h>

flux_watcher_t *zmqutil_watcher_create (flux_reactor_t *r,
                                        void *zsock,
                                        int events,
                                        flux_watcher_f cb,
                                        void *arg);

void *zmqutil_watcher_get_zsock (flux_watcher_t *w);

#endif /* !_ZMQUTIL_ZWATCHER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

