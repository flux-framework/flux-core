/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_HOSTLIST_HOSTNAME_H
#define HAVE_FLUX_HOSTLIST_HOSTNAME_H

/*
 * Convenience structure representing a hostname as a prefix,
 *  optional numeric part, and suffix.
 */
struct hostname {
    char *hostname;         /* cache of initialized hostname        */
    char *prefix;           /* hostname prefix                      */
    unsigned long num;      /* numeric suffix                       */

    /* string representation of numeric suffix
     * points into `hostname'                                       */
    char *suffix;
};

struct hostname * hostname_create (const char *s);
struct hostname * hostname_create_with_suffix (const char *s, int i);
void hostname_destroy (struct hostname *hn);

int hostname_suffix_is_valid (struct hostname *hn);
int hostname_suffix_width (struct hostname *hn);

#endif /* !HAVE_FLUX_HOSTLIST_HOSTNAME_H */
