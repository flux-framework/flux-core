/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libjob/count.h"
#include "src/common/libjob/jjc.h"

int main (int ac, char *av[])
{
    char *s;
    char *nnodes;
    char *nslots;
    char *slot_size;
    char *slot_gpus;
    int flags = COUNT_FLAG_SHORT;
    struct jjc_counts jjc;
    log_init ("jjc-reader");
    if (read_all (STDIN_FILENO, (void **) &s) < 0)
        log_err_exit ("Failed to read stdin");
    if (jjc_get_counts (s, &jjc) < 0)
        log_msg_exit ("%s", jjc.error);
    nnodes = count_encode (jjc.nnodes, flags);
    nslots = count_encode (jjc.nslots, flags);
    slot_size = count_encode (jjc.slot_size, flags);
    slot_gpus = count_encode (jjc.slot_gpus, flags);
    printf ("nodefactor=%d nnodes=%s nslots=%s slot_size=%s slot_gpus=%s exclusive=%s duration=%.1f\n",
            jjc.nodefactor,
            nnodes ? nnodes : "0",
            nslots ? nslots : "0",
            slot_size ? slot_size : "0",
            slot_gpus ? slot_gpus : "0",
            jjc.exclusive ? "true" : "false",
            jjc.duration);
    log_fini ();
    free (s);
    free (nnodes);
    free (nslots);
    free (slot_size);
    free (slot_gpus);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
