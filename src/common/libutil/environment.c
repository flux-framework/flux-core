/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"
#include "ccan/str/str.h"


#include "environment.h"

struct environment {
    zhash_t *environment;
};

void environment_destroy (struct environment *e)
{
    if (e) {
        zhash_destroy (&e->environment);
        free (e);
    }
}

struct environment *environment_create (void)
{
    struct environment *e = xzmalloc (sizeof(*e));
    if (!(e->environment = zhash_new ()))
        oom ();
    return e;
}

struct env_item {
    char *argz;
    size_t argz_len;
    char sep;
    _Bool clean;
    _Bool unset;
    char *str_cache;
};

struct env_item * new_env_item(){
    return calloc (1, sizeof(struct env_item));
}
void free_env_item(struct env_item *i)
{
    free (i->argz);
    free (i->str_cache);
    free (i);
}

char *find_env_item(struct env_item *i, const char *s)
{
    char *entry = 0;
    while ((entry = argz_next (i->argz, i->argz_len, entry))) {
        if (streq (entry, s))
            return entry;
    }
    return NULL;
}

static const char *stringify_env_item (struct env_item *item)
{
    if (!item)
        return NULL;
    if (!item->argz)
        return "";
    if (item->clean)
        return item->str_cache;

    free(item->str_cache);
    item->str_cache = malloc(item->argz_len);
    memcpy(item->str_cache, item->argz, item->argz_len);
    argz_stringify(item->str_cache, item->argz_len, item->sep);

    return item->str_cache;
}

const char *environment_get (struct environment *e, const char *key)
{
    struct env_item *item = zhash_lookup (e->environment, key);
    return stringify_env_item(item);
}

static void environment_set_inner (struct environment *e,
                                   const char *key,
                                   const char *value,
                                   char separator)
{
    struct env_item *item = new_env_item();
    item->clean = false;
    item->unset = (value == NULL);
    item->sep = separator;
    zhash_update (e->environment, key, (void *)item);
    zhash_freefn (e->environment, key, (zhash_free_fn *)free_env_item);

    environment_push_back(e, key, value);
}

void environment_set (struct environment *e,
                      const char *key,
                      const char *value,
                      char separator)
{
    environment_set_inner (e, key, value, separator);
}

void environment_unset (struct environment *e, const char *key)
{
    environment_set_inner (e, key, NULL, 0);
}

static void environment_push_inner (struct environment *e,
                                    const char *key,
                                    const char *value,
                                    bool before,
                                    bool split)
{
    if (!value || strlen (value) == 0)
        return;

    struct env_item *item = zhash_lookup (e->environment, key);
    if (!item) {
        item = new_env_item();
        zhash_update (e->environment, key, (void *)item);
        zhash_freefn (e->environment, key, (zhash_free_fn *)free_env_item);
    }

    if (split) {
        char *split_value = NULL;
        size_t split_value_len = 0;
        char *entry = NULL;

        argz_create_sep (value, item->sep, &split_value, &split_value_len);
        if (before && argz_count (split_value, split_value_len) > 1) {
            /*  If inserting a split list "before", we need to reverse
             *   the list, o/w the split list is pushed in the wrong
             *   order (last element ends up as the first element)
             */
            char *rev = NULL;
            size_t rev_len = 0;
            while ((entry = argz_next (split_value, split_value_len, entry)))
                argz_insert (&rev, &rev_len, rev, entry);
            free (split_value);
            split_value = rev;
            split_value_len = rev_len;
        }
        entry = NULL;
        while((entry = argz_next (split_value, split_value_len, entry))) {
            char *found;
            if ((!strlen(entry)))
                continue;
            /*
             * If an existing entry is found matching this entry, and
             *  the `before` flag is set, then delete the existing entry so
             *  it is effectively pushed to the front (without duplication)
             */
            if ((found = find_env_item (item, entry)) && before)
                argz_delete (&item->argz, &item->argz_len, found);
            if (before)
                argz_insert (&item->argz, &item->argz_len, item->argz, entry);
            else if (found == NULL)
                argz_add(&item->argz, &item->argz_len, entry);
        }
        free(split_value);
    } else {
        if (before) {
            argz_insert (&item->argz, &item->argz_len, item->argz, value);
        } else {
            argz_add (&item->argz, &item->argz_len, value);
        }
    }
}

void environment_push (struct environment *e,
                       const char *key,
                       const char *value)
{
    environment_push_inner (e, key, value, true, true);
}

void environment_push_back (struct environment *e,
                            const char *key,
                            const char *value)
{
    environment_push_inner (e, key, value, false, true);
}

void environment_no_dedup_push (struct environment *e,
                                const char *key,
                                const char *value)
{
    environment_push_inner (e, key, value, true, false);
}

void environment_no_dedup_push_back (struct environment *e,
                                     const char *key,
                                     const char *value)
{
    environment_push_inner (e, key, value, false, false);
}

void environment_from_env (struct environment *e,
                           const char *key,
                           const char *default_base,
                           char separator)
{
    const char *env = getenv (key);
    if (!env && !default_base)
        return;
    if (!env)
        env = default_base;
    environment_set (e, key, env, separator);
}

void environment_set_separator (struct environment *e,
                                const char *key,
                                char separator)
{
    struct env_item *item = zhash_lookup (e->environment, key);
    if (item)
        item->sep = separator;
}

const char *environment_first (struct environment *e)
{
    return stringify_env_item (zhash_first (e->environment));
}

const char *environment_next (struct environment *e)
{
    return stringify_env_item (zhash_next (e->environment));
}

const char *environment_cursor (struct environment *e)
{
    return zhash_cursor (e->environment);
}

const char *environment_var_next (struct environment *e,
                                  const char *key,
                                  const char *entry)
{
    struct env_item *item = zhash_lookup (e->environment, key);
    if (!item)
        return NULL;
    return argz_next (item->argz, item->argz_len, entry);
}

int environment_insert (struct environment *e,
                        const char *key,
                        char *before,
                        const char *value)
{
    error_t err;
    struct env_item *item = zhash_lookup (e->environment, key);
    if (!item) {
        errno = ENOENT;
        return -1;
    }
    if ((err = argz_insert (&item->argz,
                            &item->argz_len,
                            before,
                            value) != 0)) {
        errno = err;
        return -1;
    }
    return 0;
}


void environment_apply (struct environment *e)
{
    const char *key;
    struct env_item *item;
    FOREACH_ZHASH (e->environment, key, item)
    {
        if (item->unset) {
            if (unsetenv (key))
                log_err_exit ("unsetenv: %s", key);
        } else {
            const char *value = stringify_env_item (item);
            if (setenv (key, value, 1) < 0)
                log_err_exit ("setenv: %s=%s", key, value);
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
