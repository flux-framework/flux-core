/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* handle-open.c - open a Flux handle and report any error
 *
 * Usage: handle-open [--is-setuid] [URI]
 *
 * With --is-setuid, exit 0 if the real and effective user IDs differ.
 * Otherwise call flux_open_ex(3) on URI, or NULL if URI is not given,
 * and print the error text on failure.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <flux/core.h>

#include "ccan/str/str.h"

int main (int argc, char *argv[])
{
    const char *uri = NULL;
    flux_error_t error;
    flux_t *h;

    if (argc > 2) {
        fprintf (stderr, "Usage: handle-open [--is-setuid] [URI]\n");
        return 2;
    }
    if (argc == 2) {
        if (streq (argv[1], "--is-setuid"))
            return getuid () != geteuid () ? 0 : 1;
        uri = argv[1];
    }
    if (!(h = flux_open_ex (uri, 0, &error))) {
        fprintf (stderr, "flux_open: %s\n", error.text);
        return 1;
    }
    flux_close (h);
    return 0;
}

// vi:ts=4 sw=4 expandtab
