/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"
#include "common.h"

struct optparse_option info_opts[] =  {
    { .name = "original", .key = 'o', .has_arg = 0,
      .usage = "For key \"jobspec\", return the original submitted jobspec",
    },
    { .name = "base", .key = 'b', .has_arg = 0,
      .usage = "For key \"jobspec\" or \"R\", "
               "do not apply updates from eventlog",
    },
    OPTPARSE_TABLE_END
};

static void info_usage (void)
{
    fprintf (stderr,
             "Usage: flux job info id key\n"
             "some useful keys are:\n"
             "  J                    - signed jobspec\n"
             "  R                    - allocated resources\n"
             "  eventlog             - primary job eventlog\n"
             "  jobspec              - job specification\n"
             "  guest.exec.eventlog  - execution eventlog\n"
             "  guest.input          - job input log\n"
             "  guest.output         - job output log\n"
             "Use flux job info -h to list available options\n");

}

int cmd_info (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_error_t error;
    int optindex = optparse_option_index (p);
    flux_jobid_t id;
    const char *id_str;
    const char *key;
    flux_future_t *f;
    const char *val;
    char *new_val = NULL;

    // Usage: flux job info id key
    if (argc - optindex != 2) {
        info_usage ();
        exit (1);
    }
    id_str = argv[optindex++];
    id = parse_jobid (id_str);
    key = argv[optindex++];

    if (!(h = flux_open_ex (NULL, 0, &error)))
        log_msg_exit ("flux_open: %s", error.text);

    /* The --original (pre-frobnication) jobspec is obtained by fetching J.
     * J is the original jobspec, signed, so we must unwrap it to get to the
     * delicious jobspec inside.
     */
    if (optparse_hasopt (p, "original") && streq (key, "jobspec")) {
        flux_error_t error;
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "keys", "J",
                                 "flags", 0))
            || flux_rpc_get_unpack (f, "{s:s}", "J", &val) < 0)
            log_msg_exit ("%s", future_strerror (f, errno));
        if (!(new_val = flux_unwrap_string (val, false, NULL, &error)))
            log_msg_exit ("Failed to unwrap J to get jobspec: %s", error.text);
        val = new_val;
    }
    /* The current (non --base) R and jobspec are obtained through the
     * job-info.lookup RPC w/ the CURRENT flag.
     */
    else if (!optparse_hasopt (p, "base")
             && (streq (key, "R") || streq (key, "jobspec"))) {
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "keys", key,
                                 "flags", FLUX_JOB_LOOKUP_CURRENT))
            || flux_rpc_get_unpack (f, "{s:s}", key, &val) < 0)
            log_msg_exit ("%s", future_strerror (f, errno));
    }
    /* All other keys are obtained this way.
     */
    else {
        if (!(f = flux_rpc_pack (h,
                                 "job-info.lookup",
                                 FLUX_NODEID_ANY,
                                 0,
                                 "{s:I s:[s] s:i}",
                                 "id", id,
                                 "keys", key,
                                 "flags", 0))
            || flux_rpc_get_unpack (f, "{s:s}", key, &val) < 0)
            log_msg_exit ("%s", future_strerror (f, errno));
    }

    printf ("%s\n", val);

    free (new_val);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/* vi: ts=4 sw=4 expandtab
 */
