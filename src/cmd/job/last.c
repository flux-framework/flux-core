/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job last */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libjob/idf58.h"
#include "common.h"

int cmd_last (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_t *h;
    json_t *jobs;
    size_t index;
    json_t *entry;
    const char *slice = "[:1]";
    char sbuf[16];

    if (optindex < argc) {
        slice = argv[optindex++];
        // if slice doesn't contain '[', assume 'flux job last N' form
        if (!strchr (slice, '[')) {
            errno = 0;
            char *endptr;
            int n = strtol (slice, &endptr, 10);
            if (errno != 0 || *endptr != '\0') {
                optparse_print_usage (p);
                exit (1);
            }
            snprintf (sbuf, sizeof (sbuf), "[:%d]", n);
            slice = sbuf;
        }
    }
    if (optindex < argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h,
                             "job-manager.history.get",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "slice", slice))
        || flux_rpc_get_unpack (f, "{s:o}", "jobs", &jobs) < 0) {
        log_msg_exit ("%s", future_strerror (f, errno));
    }
    if (json_array_size (jobs) == 0)
        log_msg_exit ("job history is empty");
    json_array_foreach (jobs, index, entry)
        printf ("%s\n", idf58 (json_integer_value (entry)));
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}


/* vi: ts=4 sw=4 expandtab
 */
