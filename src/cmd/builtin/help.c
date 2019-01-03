/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdlib.h>

#include "src/common/libutil/setenvf.h"
#include "builtin.h"

static int no_docs_set (optparse_t *p)
{
    int *flags = optparse_get_data (p, "conf_flags");
    const char *no_docs_path = flux_conf_get ("no_docs_path", *flags);
    struct stat sb;

    /* FLUX_IGNORE_NO_DOCS environment workaround for unit tests */
    if (getenv("FLUX_IGNORE_NO_DOCS"))
        return 0;

    return (!stat (no_docs_path, &sb));
}

static int cmd_help (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    char *cmd;

    if (n < ac) {
        const char *topic = av [n];
        int rc;

        if (no_docs_set (p))
            log_msg_exit ("flux manual pages not built");

        /* N.B. Flux doc dir already prepended to MANPATH if needed.
         */

        /*  If user tried 'flux help flux*' then assume we
         *   don't need to prepend another flux.
         *  O/w, assume user is asking for help on a flux command
         *   so prepend flux-
         */
        if (strncmp (topic, "flux", 4) == 0)
            cmd = xasprintf ("man %s", topic);
        else
            cmd = xasprintf ("man flux-%s", topic);

        if ((rc = system (cmd)) < 0)
            log_err_exit ("man");
        else if (WIFEXITED (rc) && ((rc = WEXITSTATUS (rc)) != 0))
            exit (rc);
        else if (WIFSIGNALED (rc))
            log_errn_exit (1, "man: %s\n", strsignal (WTERMSIG (rc)));
        free (cmd);
    } else
        usage (optparse_get_parent (p));
    return (0);
}

int subcommand_help_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "help",
        cmd_help,
        "[OPTIONS...] [COMMAND...]",
        "Display help information for flux commands",
        0, NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
