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
#include <unistd.h>

#include "builtin.h"
#include "src/common/libutil/environment.h"
#include "ccan/str/str.h"

static void prepare_environment (void)
{
    struct environment *env;

    if (!(env = environment_create ()))
        log_err_exit ("error creating environment");
    builtin_env_add_pythonpath (env);
    environment_apply (env);
    environment_destroy (env);
}

static int cmd_python (optparse_t *p, int ac, char *av[])
{
    /*  Support `--get-path` as first argument (other args are ignored.
     *  Print installed python path and exit if found.
     */
    if (ac > 1 && streq (av[1], "--get-path")) {
        printf ("%s\n",
                flux_conf_builtin_get ("python_path", FLUX_CONF_INSTALLED));
        exit (0);
    }

    /*
     *  Ensure av[0] matches the full path of the interpreter we're executing.
     *  Other than just being good practice, this ensures that sys.executable
     *  is correct (since python uses sys.argv[0] for sys.executable) and so
     *  that symlink'd binaries in virtualenvs are respected.
     */
    av[0] = PYTHON_INTERPRETER;

    prepare_environment ();

    execv (PYTHON_INTERPRETER, av); /* no return if successful */
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
        "Run the python interpreter flux was configured with",
        OPTPARSE_SUBCMD_SKIP_OPTS,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
