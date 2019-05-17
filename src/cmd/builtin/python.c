/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <unistd.h>

#include "builtin.h"
#include "config.h"
#include "src/common/libutil/environment.h"

static int cmd_python (optparse_t *p, int ac, char *av[])
{
    execv (PYTHON_INTERPRETER, av); /* no return if sucessful */
    log_err_exit ("execvp (%s)", PYTHON_INTERPRETER);
    return (0);
}

int subcommand_python_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
                                 "python",
                                 cmd_python,
                                 "[PYTHON ARGUMENTS...]",
                                 "Run the python interpreter flux was "
                                 "configured with",
                                 OPTPARSE_SUBCMD_SKIP_OPTS,
                                 NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
