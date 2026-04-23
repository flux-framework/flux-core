/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* imp_exec_helper.c - produce IMP exec helper JSON on stdout
 *
 * Usage: flux imp_exec_helper JOBID
 *
 * Fetch the signed jobspec (J) for JOBID and emit it as JSON to stdout
 * for consumption by the IMP exec helper protocol (RFC 15).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>

#include "builtin.h"

static int helper (optparse_t *p, int ac, char *av[])
{
    int optindex;
    flux_t *h;
    flux_future_t *f;
    flux_jobid_t id;
    const char *idstr = NULL;
    const char *J;
    json_t *imp_input;

    log_init ("imp_exec_helper");

    optindex = optparse_option_index (p);
    if (optindex != ac && optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < ac)
        idstr = av[optindex];
    else if (!(idstr = getenv ("FLUX_JOB_ID")))
        log_msg_exit ("Unable to determine jobid");
    if (flux_job_id_parse (idstr, &id) < 0)
        log_err_exit ("invalid jobid: %s", idstr);

    h = builtin_get_flux_handle (p);

    if (!(f = flux_rpc_pack (h,
                             "job-info.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:[s] s:i}",
                             "id", id,
                             "keys", "J",
                             "flags", 0))
        || flux_rpc_get_unpack (f, "{s:s}", "J", &J) < 0)
        log_err_exit ("error looking up J: %s", future_strerror (f, errno));

    if (!(imp_input = json_pack ("{s:s}", "J", J)))
        log_msg_exit ("error creating imp input object");

    if (json_dumpf (imp_input, stdout, JSON_COMPACT) < 0)
        log_msg_exit ("error writing imp input object to stdout");

    json_decref (imp_input);
    flux_future_destroy (f);
    return 0;
}

int subcommand_imp_exec_helper_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "imp_exec_helper",
        helper,
        "JOBID",
        "Emit IMP exec helper JSON for JOBID",
        0,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
