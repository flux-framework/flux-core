/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job namespace */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "common.h"

static void print_job_namespace (const char *src)
{
    char ns[64];
    flux_jobid_t id = parse_jobid (src);
    if (flux_job_kvs_namespace (ns, sizeof (ns), id) < 0)
        log_msg_exit ("error getting kvs namespace for %s", src);
    printf ("%s\n", ns);
}

int cmd_namespace (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin)))
            print_job_namespace (trim_string (src));
    }
    else {
        while (optindex < argc)
            print_job_namespace (argv[optindex++]);
    }
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
