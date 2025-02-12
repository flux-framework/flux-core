/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_TRUNCATE_H
#define _FLUX_RESOURCE_TRUNCATE_H

#include <jansson.h>

#include <flux/core.h>
#include <flux/idset.h>

struct truncate_info *truncate_info_create ();
void truncate_info_destroy (struct truncate_info *ti);
int truncate_info_update (struct truncate_info *ti, json_t *event);

json_t *truncate_info_event (struct truncate_info *ti);

#endif /* ! _FLUX_RESOURCE_TRUNCATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
