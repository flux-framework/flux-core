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
#include <czmq.h>
#include <argz.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"


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
        if (!strcmp(entry, s))
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
        argz_create_sep (value, item->sep, &split_value, &split_value_len);
        char *entry = 0;
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
