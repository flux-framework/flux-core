/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job id */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "common.h"

struct optparse_option id_opts[] = {
    { .name = "to", .key = 't', .has_arg = 1,
      .arginfo = "dec|kvs|hex|dothex|words|f58",
      .usage = "Convert jobid to specified form",
    },
    OPTPARSE_TABLE_END
};

static void id_convert (optparse_t *p, const char *src, char *dst, int dstsz)
{
    const char *to = optparse_get_str (p, "to", "dec");
    flux_jobid_t id;

    /*  Parse as any valid JOBID
     */
    if (flux_job_id_parse (src, &id) < 0)
        log_msg_exit ("%s: malformed input", src);

    /*  Now encode into requested representation:
     */
    if (flux_job_id_encode (id, to, dst, dstsz) < 0) {
        if (errno == EPROTO)
            log_msg_exit ("Unknown to=%s", to);
        log_msg_exit ("Unable to encode id %s to %s", src, to);
    }
}

int cmd_id (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    char dst[256];

    /*  Require at least one id to be processed for success.
     *  Thus, `flux job id` with no input or args will exit with
     *   non-zero exit code.
     */
    int rc = -1;

    if (optindex == argc) {
        char src[256];
        while ((fgets (src, sizeof (src), stdin))) {
            id_convert (p, trim_string (src), dst, sizeof (dst));
            printf ("%s\n", dst);
            rc = 0;
        }
    }
    else {
        while (optindex < argc) {
            id_convert (p, argv[optindex++], dst, sizeof (dst));
            printf ("%s\n", dst);
            rc = 0;
        }
    }
    return rc;
}


/* vi: ts=4 sw=4 expandtab
 */
