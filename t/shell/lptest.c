/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* lptest.c - ripple test */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>

void lptest (int length, int count)
{
    int i;
    int j;

    for (i = 0; i < count; i++) {
        for (j = 0; j < length; j++)
            putchar (0x21 + ((i + j) % 0x5e)); // charset: !(0x21) thru ~(0x7e)
        putchar ('\n');
    }
}

int main (int ac, char **av)
{
    int length = 79;
    int count = 200;

    if (ac > 3) {
        fprintf (stderr, "Usage: %s [length] [count]\n", av[0]);
        return 1;
    }
    if (ac > 2)
        count = strtoul (av[2], NULL, 10);
    if (ac > 1)
        length = strtoul (av[1], NULL, 10);
    lptest (length, count);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

