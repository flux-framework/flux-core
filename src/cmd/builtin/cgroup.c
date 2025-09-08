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
#include "builtin.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/cgroup.h"

static int subcmd_path (optparse_t *p, int argc, char *argv[])
{
    struct cgroup_info cgroup;
    int n = optparse_option_index (p);

    if (n < argc)
        log_msg_exit ("this command does not accept free arguments");
    if (cgroup_info_init (&cgroup) < 0)
        log_err_exit ("incompatible cgroup configuration");
    printf ("%s\n", cgroup.path);
    return 0;
}

static struct optparse_subcommand cgroup_subcmds[] = {
    { "path",
      "[OPTIONS]",
      "Print path to cgroup directory",
      subcmd_path,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

static int cmd_cgroup (optparse_t *p, int argc, char *argv[])
{
    log_init ("flux-cgroup");

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        exit (1);

    return 0;
}

int subcommand_cgroup_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
                                 "cgroup",
                                 cmd_cgroup,
                                 NULL,
                                 "cgroup utility",
                                 0,
                                 NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "cgroup"),
                                  cgroup_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
