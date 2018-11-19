/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
