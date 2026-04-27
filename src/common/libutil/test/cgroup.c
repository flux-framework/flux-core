/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <unistd.h>
#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"

#include "cgroup.h"

void test_cpu (struct cgroup_info *cgroup)
{
    const char *keys[] = {"usage_usec", "user_usec", "system_usec"};

    skip (access (cgroup_path_to (cgroup, "cpu.stat"), R_OK) < 0,
          ARRAY_SIZE (keys),
          "cpu.stat (unavailable)");

    for (int i = 0; i < ARRAY_SIZE (keys); i++) {
        int rc;
        unsigned long long u64 = 0;

        rc = cgroup_key_scanf (cgroup, "cpu.stat", keys[i], "%llu", &u64);
        ok (rc == 1, "cgroup_scanf scanned cpu.stat:%s", keys[i]);
        diag ("cpu.stat:%s=%llu", keys[i], u64);
    }

    end_skip;
}

void test_memory (struct cgroup_info *cgroup)
{
    const char *names[] = {"memory.current", "memory.peak"};

    for (int i = 0; i < ARRAY_SIZE (names); i++) {
        int rc;
        unsigned long long u64 = 0;

        skip (access (cgroup_path_to (cgroup, names[i]), R_OK) < 0,
              1,
              "%s (unavailable)",
              names[i]);

        rc = cgroup_scanf (cgroup, names[i], "%llu", &u64);
        ok (rc == 1, "cgroup_scanf scanned %s", names[i]);
        diag ("%s=%llu", names[i], u64);

        end_skip;
    }

    const char *keys[] = {"low", "high", "max", "oom", "oom_kill"};

    skip (access (cgroup_path_to (cgroup, "memory.events"), R_OK) < 0,
          ARRAY_SIZE (keys),
          "memory.events (unavailable)");

    for (int i = 0; i < ARRAY_SIZE (keys); i++) {
        int rc;
        unsigned long long u64 = 0;

        rc = cgroup_key_scanf (cgroup, "memory.events", keys[i], "%llu", &u64);
        ok (rc == 1, "cgroup_scanf scanned memory.events:%s", keys[i]);
        diag ("memory.events:%s=%llu", keys[i], u64);
    }

    end_skip;
}

void test_init_pid (struct cgroup_info *cgroup)
{
    struct cgroup_info cg;
    int rc;

    /* cgroup_info_init_pid (getpid()) should give the same path as
     * cgroup_info_init(), since cgroup_info_init() is defined in terms of it.
     */
    rc = cgroup_info_init_pid (&cg, getpid ());
    ok (rc == 0, "cgroup_info_init_pid (getpid()) succeeds");

    skip (rc < 0, 1, "cgroup_info_init_pid (getpid()) failed");
    ok (strcmp (cgroup->path, cg.path) == 0,
        "cgroup_info_init_pid (getpid()) path matches cgroup_info_init()");
    if (rc == 0)
        diag ("path: %s", cg.path);
    end_skip;

    /* pid 1 (init/systemd) is always present and should have a cgroup */
    rc = cgroup_info_init_pid (&cg, 1);
    ok (rc == 0, "cgroup_info_init_pid (1) succeeds");
    if (rc == 0)
        diag ("pid 1 cgroup: %s", cg.path);

    /* pid 0 (idle process) has no /proc/0 entry and should fail */
    rc = cgroup_info_init_pid (&cg, 0);
    ok (rc < 0, "cgroup_info_init_pid (0) fails as expected");
}

int main(int argc, char** argv)
{
    struct cgroup_info cgroup;

    if (cgroup_info_init (&cgroup) < 0) {
        diag ("incompatible cgroup configuration");
        plan (SKIP_ALL);
    }
    else {
        plan (NO_PLAN);
        test_cpu (&cgroup);
        test_memory (&cgroup);
        test_init_pid (&cgroup);
    }
    done_testing();
}

// vi:ts=4 sw=4 expandtab
