/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_TASKMAP_PRIVATE_H
#define _UTIL_TASKMAP_PRIVATE_H

#include <jansson.h>
#include <flux/taskmap.h>

json_t *taskmap_encode_json (const struct taskmap *map, int flags);

struct taskmap *taskmap_decode_json (json_t *o, flux_error_t *errp);

#endif /* !_UTIL_TASKMAP_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
