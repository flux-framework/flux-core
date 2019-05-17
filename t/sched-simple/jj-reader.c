/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/modules/sched-simple/libjj.h"

int main (int ac, char *av[])
{
    char *s;
    struct jj_counts jj;
    log_init ("jj-reader");
    if (read_all (STDIN_FILENO, (void **)&s) < 0)
        log_err_exit ("Failed to read stdin");
    if (libjj_get_counts (s, &jj) < 0)
        log_msg_exit ("%s", jj.error);
    printf ("nnodes=%d nslots=%d slot_size=%d\n",
            jj.nnodes,
            jj.nslots,
            jj.slot_size);
    log_fini ();
    free (s);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
