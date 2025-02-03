/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_SDBUS_H
#define _SDBUS_SDBUS_H

#include <flux/core.h>

struct sdbus_ctx *sdbus_ctx_create (flux_t *h,
                                    int argc,
                                    char **argv,
                                    flux_error_t *error);
void sdbus_ctx_destroy (struct sdbus_ctx *ctx);

#endif /* !_SDBUS_SDBUS_H */

// vi:ts=4 sw=4 expandtab
