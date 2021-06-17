/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobspec1-validate.c - validate jobspec v1 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/log.h"

int main (int ac, char **av)
{
    void *buf;
    flux_jobspec1_t *jobspec;
    char errbuf[128];

    log_init ("jobspec1-validate");

    if (ac != 1) {
        fprintf (stderr, "Usage: %s <infile\n", av[0]);
        return 1;
    }

    if (read_all (STDIN_FILENO, &buf) < 0)
        log_err_exit ("read stdin");

    if (!(jobspec = flux_jobspec1_decode (buf, errbuf, sizeof (errbuf))))
        log_msg_exit ("%s", errbuf);
    if (flux_jobspec1_check (jobspec, errbuf, sizeof (errbuf)) < 0)
        log_msg_exit ("%s", errbuf);

    flux_jobspec1_destroy (jobspec);
    free (buf);
    return 0;
}

// vi:ts=4 sw=4 expandtab
