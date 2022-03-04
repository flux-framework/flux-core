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
#include <flux/core.h>

#include "strtab.h"

static const char *format_entry (struct strtab *entry, const char *fmt)
{
    switch (fmt ? *fmt : 0) {
        case 's':
            return entry->short_lower;
        case 'S':
            return entry->short_upper;
        case 'l':
            return entry->long_lower;
        case 'L':
        default:
            return entry->long_upper;
    }
}

const char *strtab_numtostr (int num,
                             const char *fmt,
                             struct strtab *strtab,
                             size_t count)
{
    struct strtab unknown = { 0, "(unknown)", "(unknown)", "?", "?" };

    for (int i = 0; i < count; i++)
        if (strtab[i].num == num)
            return format_entry (&strtab[i], fmt);
    return format_entry (&unknown, fmt);
}

int strtab_strtonum (const char *s, struct strtab *strtab, size_t count)
{
    if (s) {
        for (int i = 0; i < count; i++) {
            if (!strcmp (strtab[i].short_lower, s)
                || !strcmp (strtab[i].short_upper, s)
                || !strcmp (strtab[i].long_lower, s)
                || !strcmp (strtab[i].long_upper, s))
                return strtab[i].num;
        }
    }
    errno = EINVAL;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
