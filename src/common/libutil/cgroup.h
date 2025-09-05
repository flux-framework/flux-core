/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef UTIL_CGROUP_H
#define UTIL_CGROUP_H 1

#include <sys/param.h>
#include <stdbool.h>
#include <stdarg.h>

struct cgroup_info {
    char mount_dir[PATH_MAX + 1];
    char path[PATH_MAX + 1];
    bool unified;
};

int cgroup_info_init (struct cgroup_info *cgroup);

int cgroup_access (struct cgroup_info *cgroup, const char *name, int mode);

/* Parse value from cgroup file
 */
int cgroup_scanf (struct cgroup_info *cgroup,
                  const char *name,
                  const char *fmt,
                  ...);

/* Parse one value from a flat-keyed cgroup file
 */
int cgroup_key_scanf (struct cgroup_info *cgroup,
                      const char *name,
                      const char *key,
                      const char *fmt,
                      ...);


#endif /* !UTIL_CGROUP_H */
