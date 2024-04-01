/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <unistd.h>

#include "builtin.h"

static void query_continuation (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    const char *row;

    if (flux_rpc_get (f, &row) < 0) {
        if (errno == ENODATA)
            flux_reactor_stop (r);
        else {
            log_msg ("%s", future_strerror (f, errno));
            flux_reactor_stop_error (r);
        }
        return;
    }
    printf ("%s\n", row);
    flux_future_reset (f);
}

static int cmd_sql (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    int e;
    char *argz = NULL;
    size_t argz_len = 0;
    flux_t *h;
    flux_future_t *f;
    int rc;

    log_init ("flux-sql");
    if (n == ac) {
        optparse_print_usage (p);
        exit (1);
    }
    /* Make one SQL query string from one or more query arguments
     */
    if ((e = argz_create (&av[n], &argz, &argz_len)) != 0)
        log_errn_exit (e, "error processing arguments");
    argz_stringify (argz, argz_len, ' ');

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("could not contact flux broker");

    if (!(f = flux_rpc_pack (h,
                             "job-sql.query",
                             0,
                             FLUX_RPC_STREAMING,
                             "{s:s}",
                             "query", argz))
        || flux_future_then (f, -1, query_continuation, p) < 0)
        log_err_exit ("error sending query");
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    flux_future_destroy (f);

    flux_close (h);
    free (argz);
    return rc;
}

int subcommand_sql_register (optparse_t *p)
{
    if (optparse_reg_subcommand (p,
                                 "sql",
                                 cmd_sql,
                                 "[OPTIONS...] QUERY",
                                 "Query the SQL job database",
                                 0,
                                 NULL) != OPTPARSE_SUCCESS)
        return -1;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
