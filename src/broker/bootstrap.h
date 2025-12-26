/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_BOOTSTRAP_H
#define BROKER_BOOTSTRAP_H

#include <flux/core.h>
#include "src/common/libpmi/upmi.h"
#include "src/common/libpmi/bizcache.h"
#include "broker.h"

/* temporarily exposed during refactor */
struct bootstrap {
    struct broker *ctx;
    struct upmi *upmi;
    struct bizcache *cache;
    bool under_flux;
    bool finalized;
};

struct bootstrap *bootstrap_create (struct broker *ctx,
                                    struct upmi_info *info,
                                    flux_error_t *errp);
void bootstrap_destroy (struct bootstrap *boot);

int bootstrap_finalize (struct bootstrap *boot, flux_error_t *errp);

const char *bootstrap_method (struct bootstrap *boot);

#endif /* !BROKER_BOOTSTRAP_H */

// vi:ts=4 sw=4 expandtab
