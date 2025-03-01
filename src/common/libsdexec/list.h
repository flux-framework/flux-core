/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_LIST_H
#define _LIBSDEXEC_LIST_H

#include <jansson.h>
#include <flux/core.h>

struct unit_info {
    const char *name;
    const char *description;
    const char *load_state;
    const char *active_state;
    const char *sub_state;
    const char *name_follower;  // "" if no unit whose state follows this one
    const char *path;

    json_int_t job_id;          // 0 if no job queued for the unit
    const char *job_type;
    const char *job_path;
};

/* Obtain the unit list, filtered by glob pattern.
 * (E.g. use "*" for all).
 */
flux_future_t *sdexec_list_units (flux_t *h,
                                  const char *service,
                                  uint32_t rank,
                                  const char *pattern);

/* Iterate over returned list of units.
 * Fill in 'info' with the next entry and return true,
 * or return false if there are no more entries.
 */
bool sdexec_list_units_next (flux_future_t *f, struct unit_info *info);

#endif /* !_LIBSDEXEC_LIST_H */

// vi:ts=4 sw=4 expandtab
