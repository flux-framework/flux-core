/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_BROKERCFG_H
#define BROKER_BROKERCFG_H

#include <flux/core.h>
#include "attr.h"
#include "modhash.h"

struct brokercfg;

struct brokercfg *brokercfg_create (flux_t *h,
                                    const char *path,
                                    attr_t *attr,
                                    modhash_t *modhash);
void brokercfg_destroy (struct brokercfg *cfg);

#endif /* !BROKER_BROKERCFG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
