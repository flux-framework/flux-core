/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_AFFINITY_H
#define _SHELL_AFFINITY_H

#include <hwloc.h>

/*  Parse a list of hwloc bitmap strings from `setlist` in list, bitmask,
 *  or taskset form and return an allocated hwloc_cpuset_t array of size
 *  `ntasks` filled with the resulting bitmasks. If `ntasks` is greater
 *  than the number of provided cpusets, then cpusets are reused as
 *  necessary.
 *
 *  It is an error if any cpuset is not contained within the `all` cpuset.
 */
hwloc_cpuset_t *parse_cpuset_list (const char *setlist,
                                   hwloc_cpuset_t *all,
                                   int ntasks);

/*  Create an empty hwloc_cpuset_t array of size elements
 */
hwloc_cpuset_t *cpuset_array_create (int size);

/*  Free memory for hwloc_cpuset_t array returned from cpuset_array_create()
 *  or parse_cpuset_list().
 */
void cpuset_array_destroy (hwloc_cpuset_t *set, int size);

#endif /* !_SHELL_AFFINITY_H */

/* vi: ts=4 sw=4 expandtab
 */

