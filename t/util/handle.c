/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* handle.c - test util for flux_t handle */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "ccan/str/str.h"

void usage (void)
{
    fprintf (stderr, "Usage: handle getopt u8|u32 name\n");
    exit (1);
}

void getopt (flux_t *h, const char *type, const char *name)
{
    if (streq (type, "u8")) {
        uint8_t val;
        if (flux_opt_get (h, name, &val, sizeof (val)) < 0)
            log_err_exit ("%s", name);
        printf ("%u\n", (unsigned int)val);
    }
    else if (streq (type, "u32")) {
        uint32_t val;
        if (flux_opt_get (h, name, &val, sizeof (val)) < 0)
            log_err_exit ("%s", name);
        printf ("%u\n", (unsigned int)val);
    }
    else
        usage ();
}

int main (int argc, char *argv[])
{
    flux_t *h;

    if (argc != 4)
        usage();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (streq (argv[1], "getopt"))
        getopt (h, argv[2], argv[3]);
    else
        usage ();

    flux_close (h);

    return (0);
}

// vi:ts=4 sw=4 expandtab
