/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* testloader.c - no-op module loader for testing */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

int die (const char *msg)
{
    fprintf (stderr, "%s\n", msg);
    exit (1);
}
int die_error (const char *msg, flux_error_t *error)
{
    fprintf (stderr, "%s: %s\n", msg, error->text);
    exit (1);
}

int main (int argc, char **argv)
{
    const char *uri;
    char *modargs;
    flux_error_t error;
    flux_t *h;
    int errnum = 0;

    if (!(uri = getenv ("FLUX_MODULE_URI")))
        die ("FLUX_MODULE_URI is not set");
    if (argc != 2)
        die ("loader requires a path argument");
    if (!(h = flux_open_ex (uri, 0, &error)))
        die_error ("flux_open", &error);
    if (flux_module_initialize (h, &modargs, &error) < 0)
        die_error ("flux_module_initialize", &error);
    if (flux_module_register_handlers (h, &error) < 0)
        die_error ("flux_module_register_handlers", &error);

    /* Replace flux_reactor_run with call into module's mod_main().
     * Use path, modargs.
     */
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        errnum = errno;

    if (flux_module_finalize (h, errnum, &error) < 0)
        die_error ("flux_module_finalize", &error);

    free (modargs);
    flux_close (h);

    return 0;
}

// vi:ts=4 sw=4 expandtab
