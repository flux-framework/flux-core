/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif  /* !HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include "hostname.h"

#define INVALID_CHARS ",[]\t "

/*
 * return the location of the last char in the hostname prefix
 */
static int host_prefix_end (const char *hostname)
{
    int len = strlen (hostname);
    int idx = len - 1;

    /* Rewind to the last non-digit in hostname
     */
    while (idx >= 0 && isdigit ((char) hostname[idx]))
        idx--;

    return idx;
}

/*  Return < 0 if hostname is not valid (e.g. contains invalid characters),
 *  O/w, return the length of the string.
 */
static int hostname_len (const char *hostname)
{
    int len = strlen (hostname);
    for (int i = 0; i < len; i++)
        if (strchr (INVALID_CHARS, hostname[i]))
            return -1;
    return len;
}

struct hostname * hostname_create_with_suffix (const char *hostname,
                                               int idx)
{
    struct hostname * hn = NULL;
    int len;
    char *p = NULL;

    if (!hostname || (len = hostname_len (hostname)) < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!(hn = calloc (1, sizeof (*hn))))
        return NULL;

    if (!(hn->hostname = strdup (hostname))) {
        hostname_destroy (hn);
        return NULL;
    }

    hn->num = 0;
    hn->prefix = NULL;
    hn->suffix = NULL;

    if (idx == strlen (hostname) - 1) {
        if ((hn->prefix = strdup (hostname)) == NULL) {
            hostname_destroy (hn);
            return NULL;
        }
        return hn;
    }

    hn->suffix = hn->hostname + idx + 1;
    errno = 0;
    hn->num = strtoul (hn->suffix, &p, 10);
    if (p == hn->suffix || errno != 0) {
        hostname_destroy (hn);
        errno = EINVAL;
        return NULL;
    }
    if (*p == '\0') {
        if (!(hn->prefix = malloc ((idx + 2) * sizeof (char)))) {
            hostname_destroy (hn);
            return NULL;
        }
        memcpy (hn->prefix, hostname, idx + 1);
        hn->prefix[idx + 1] = '\0';
    }
    return hn;
}


/*
 * create a struct hostname * object from a string hostname
 */
struct hostname * hostname_create (const char *hostname)
{
    if (!hostname) {
        errno = EINVAL;
        return NULL;
    }
    return hostname_create_with_suffix (hostname,
                                        host_prefix_end (hostname));
}

/* free a hostname object
 */
void hostname_destroy (struct hostname * hn)
{
    int saved_errno = errno;
    if (hn) {
        hn->suffix = NULL;
        free (hn->hostname);
        free (hn->prefix);
        free (hn);
    }
    errno = saved_errno;
}

/* return true if the hostname has a valid numeric suffix
 */
int hostname_suffix_is_valid (struct hostname * hn)
{
    return (hn && hn->suffix != NULL);
}

/* return the width (in characters) of the numeric part of the hostname
 */
int hostname_suffix_width (struct hostname * hn)
{
    if (!hn) {
        errno = EINVAL;
        return -1;
    }
    if (!hn->suffix)
        return 0;
    return strlen (hn->suffix);
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
