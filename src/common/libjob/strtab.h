/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#include <sys/types.h>

#ifndef _LIBJOB_STRTAB_H
#define _LIBJOB_STRTAB_H

struct strtab {
    int num;
    const char *long_upper;
    const char *long_lower;
    const char *short_upper;
    const char *short_lower;
};

const char *strtab_numtostr (int num,
                             const char *fmt,
                             struct strtab *strtab,
                             size_t count);

int strtab_strtonum (const char *s, struct strtab *strtab, size_t count);

#endif // !_LIBJOB_STRTAB_H

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
