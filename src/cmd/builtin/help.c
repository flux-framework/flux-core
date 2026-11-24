/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/libutil/setenvf.h"
#include "ccan/str/str.h"
#include "builtin.h"

static int no_docs_set (optparse_t *p)
{
    int *flags = optparse_get_data (p, "conf_flags");
    const char *no_docs_path = flux_conf_builtin_get ("no_docs_path", *flags);
    struct stat sb;

    /* FLUX_IGNORE_NO_DOCS environment workaround for unit tests */
    if (getenv("FLUX_IGNORE_NO_DOCS"))
        return 0;

    return (!stat (no_docs_path, &sb));
}

static int cmd_help (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);

    if (n < ac) {
        const char *topic = av [n];
        char *argv[] = { "man", "--", NULL, NULL };

        if (no_docs_set (p))
            log_msg_exit ("flux manual pages not built");

        /* N.B. Flux doc dir already prepended to MANPATH if needed.
         */

        /*  If user tried 'flux help flux*' then assume we
         *   don't need to prepend another flux.
         *  O/w, assume user is asking for help on a flux command
         *   so prepend flux-
         */
        if (strstarts (topic, "flux"))
            argv[2] = xstrdup (topic);
        else
            argv[2] = xasprintf ("flux-%s", topic);

        /*  Exec man(1) directly rather than via the shell. The "--"
         *  keeps the page name from being parsed as an option.
         */
        fflush (stdout);
        execvp ("man", argv);
        log_err_exit ("man");
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
