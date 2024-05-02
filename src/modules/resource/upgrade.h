/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_UPGRADE_H
#define _FLUX_RESOURCE_UPGRADE_H

#include <jansson.h>
#include <flux/core.h>

int upgrade_eventlog (flux_t *h, json_t **eventlog);

#endif /* !_FLUX_RESOURCE_UPGRADE_H */

// vi:ts=4 sw=4 expandtab
