/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SCHED_LIBJJ_H
#define HAVE_SCHED_LIBJJ_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#define JJ_ERROR_TEXT_LENGTH 256

struct jj_counts {
    int nnodes;    /* total number of nodes requested */
    int nslots;    /* total number of slots requested */
    int slot_size; /* number of cores per slot        */

    char error[JJ_ERROR_TEXT_LENGTH]; /* On error, contains error description */
};

/*  Parse jobspec from json string `spec`, return resource request summary
 *   in `counts` on success.
 *  Returns 0 on success and -1 on failure with errno set and jj->error[]
 *   with an error message string.
 */
int libjj_get_counts (const char *spec, struct jj_counts *counts);

#endif /* !HAVE_SCHED_LIBJJ_H */
