/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job memo */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"
#include "common.h"

struct optparse_option memo_opts[] = {
    { .name = "volatile", .has_arg = 0,
      .usage = "Memo will not appear in eventlog (will be lost on restart)",
    },
    OPTPARSE_TABLE_END
};

int cmd_memo (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_jobid_t id;
    json_t *memo = NULL;
    flux_future_t *f;
    int i;

    if ((argc - optindex) < 2) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    id = parse_jobid (argv[optindex]);

    /*  Build memo object from multiple values */
    for (i = optindex + 1; i < argc; i++) {
        void *valbuf = NULL;
        char *key;
        char *value;
        json_t *val;

        if (!(key = strdup (argv[i])))
            log_msg_exit ("memo: out of memory duplicating key");
        value = strchr (key, '=');
        if (!value)
            log_msg_exit ("memo: no value for key=%s", key);
        *value++ = '\0';

        if (streq (value, "-")) {
            ssize_t size;
            if ((size = read_all (STDIN_FILENO, &valbuf)) < 0)
                log_err_exit ("read_all");
            value = valbuf;
        }

        /* if value is not legal json, assume string */
        if (!(val = json_loads (value, JSON_DECODE_ANY, NULL))) {
            if (!(val = json_string (value)))
                log_msg_exit ("json_string");
        }

        if (!(memo = jpath_set_new (memo, key, val)))
            log_err_exit ("failed to set %s=%s in memo object\n", key, value);

        free (valbuf);
        free (key);
    }

    if (!(f = flux_rpc_pack (h,
                             "job-manager.memo",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:b s:o}",
                             "id", id,
                             "volatile", optparse_hasopt (p, "volatile"),
                             "memo", memo)))
        log_err_exit ("flux_rpc_pack");

    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("memo: %s", future_strerror (f, errno));

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/* vi: ts=4 sw=4 expandtab
 */
