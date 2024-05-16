/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_BOOT_FLUB_H
#define BROKER_BOOT_FLUB_H

#include <flux/core.h>

#include "broker.h"

int boot_flub (struct broker *ctx, flux_error_t *error);

#endif /* BROKER_BOOT_FLUB_H */

// vi:ts=4 sw=4 expandtab
