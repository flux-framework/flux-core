/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* list.c - list units
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "ccan/ptrint/ptrint.h"

#include "list.h"

static int parse_unit (json_t *units, size_t index, struct unit_info *info)
{
    json_t *entry;

    if (!units
        || !info
        || !(entry = json_array_get (units, index))) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack (entry,
                     "[sssssssIss]",
                     &info->name,
                     &info->description,
                     &info->load_state,
                     &info->active_state,
                     &info->sub_state,
                     &info->name_follower,
                     &info->path,
                     &info->job_id,
                     &info->job_type,
                     &info->job_path) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

bool sdexec_list_units_next (flux_future_t *f, struct unit_info *infop)
{
    json_t *units;
    struct unit_info info;
    int index = ptr2int (flux_future_aux_get (f, "index")); // zero if not set

    if (!infop
        || flux_rpc_get_unpack (f, "{s:[o]}", "params", &units) < 0
        || parse_unit (units, index, &info) < 0
        || flux_future_aux_set (f, "index", int2ptr (index + 1), NULL) < 0)
        return false;

    *infop = info;
    return true;
}

/* N.B. Input params:  states=[], patterns=[pattern].
 */
flux_future_t *sdexec_list_units (flux_t *h,
                                  const char *service,
                                  uint32_t rank,
                                  const char *pattern)
{
    if (!h || !pattern || !service) {
        errno = EINVAL;
        return NULL;
    }
    char topic[256];
    snprintf (topic, sizeof (topic), "%s.call", service);
    return flux_rpc_pack (h,
                          topic,
                          rank,
                          0,
                          "{s:s s:[[] [s]]}",
                          "member", "ListUnitsByPatterns",
                          "params", pattern);
}

// vi:ts=4 sw=4 expandtab
