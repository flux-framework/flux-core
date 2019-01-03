/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <config.h>

#include "builtin.h"
#if HAVE_FLUX_SECURITY_VERSION_H
#include <flux/security/version.h>
#endif


static void print_broker_version (optparse_t *p)
{
    const char *uri = getenv ("FLUX_URI");
    const char *version;

    if (!uri)
        return;
    flux_t *h = builtin_get_flux_handle (p);
    if (!h)
        log_err_exit ("flux_open %s failed", uri);
    if (!(version = flux_attr_get (h, "version")))
        log_err_exit ("flux_attr_get");
    printf ("broker:  \t\t%s\n", version);
    printf ("FLUX_URI:\t\t%s\n", uri);
}

static int cmd_version (optparse_t *p, int ac, char *av[])
{
    printf ("commands:    \t\t%s\n", FLUX_CORE_VERSION_STRING);
    printf ("libflux-core:\t\t%s\n", flux_core_version_string ());
#if HAVE_FLUX_SECURITY
    /* N.B. flux_security_version_string () was added at the same
     * time as the FLUX_SECURITY_VERSION_STRING macro.
     * The inner #ifdef may be removed after configure is enforcing a
     * pkg-config minimum version for flux-security.
     */
# ifdef FLUX_SECURITY_VERSION_STRING
    printf ("libflux-security:\t%s\n", flux_security_version_string ());
# endif
#endif
    print_broker_version (p);

    return (0);
}

int subcommand_version_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "version",
        cmd_version,
        NULL,
        "Display flux version information",
        0,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
