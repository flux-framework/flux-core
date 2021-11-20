/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* ucache.c - simple username cache
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <pwd.h>
#include <stdio.h>

#include "top.h"

struct ucache_entry {
    uid_t id;
    char name[9];
};

struct ucache {
    struct ucache_entry *users;
    size_t len;
};

/* Add a new entry to the end of the cache.
 * If memory allocation fails, return NULL with errno set.
 */
static const char *ucache_add (struct ucache *ucache,
                               uid_t userid,
                               const char *name)
{
    struct ucache_entry *new_users;
    struct ucache_entry *entry;

    if (!(new_users = realloc (ucache->users,
                               sizeof (ucache->users[0]) * (ucache->len + 1))))
        return NULL;
    ucache->users = new_users;
    entry = &ucache->users[ucache->len++];

    entry->id = userid;
    snprintf (entry->name, sizeof (entry->name), "%s", name);
    return entry->name;
}

/* Find userid in cache and return username.
 * If not found, look up in password file, add to cache, and return username.
 * If memory allocation or user lookup fails, return NULL with errno set.
 */
const char *ucache_lookup (struct ucache *ucache, uid_t userid)
{
    struct passwd *pwd;

    for (int i = 0; i < ucache->len; i++) {
        if (ucache->users[i].id == userid)
            return ucache->users[i].name;
    }
    if (!(pwd = getpwuid (userid)))
        return NULL;
    return ucache_add (ucache, userid, pwd->pw_name);
}

void ucache_destroy (struct ucache *ucache)
{
    if (ucache) {
        int saved_errno = errno;
        free (ucache->users);
        free (ucache);
        errno = saved_errno;
    }
}

struct ucache *ucache_create (void)
{
    struct ucache *ucache;

    if (!(ucache = calloc (1, sizeof (*ucache))))
        return NULL;
    return ucache;
}

// vi:ts=4 sw=4 expandtab
