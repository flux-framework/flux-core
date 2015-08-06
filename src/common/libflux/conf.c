/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/sds.h"
#include "src/common/libutil/vec.h"

#include "conf.h"

static void initialize_environment(flux_conf_t cf); 

struct env_item {
    sds sep;
    vec_str_t components;
    sds string_cache;
    bool clean;
};

struct flux_conf_struct {
    char *confdir;
    zconfig_t *z;
    zhash_t *environment;
};

struct flux_conf_itr_struct {
    flux_conf_t cf;
    zlist_t *zl;
    const char *item;
};

void flux_conf_destroy (flux_conf_t cf)
{
    if (cf->confdir)
        free (cf->confdir);
    if (cf->z)
        zconfig_destroy (&cf->z);
    if (cf->environment)
        zhash_destroy(&cf->environment);
    free (cf);
}

flux_conf_t flux_conf_create (void)
{
    flux_conf_t cf = xzmalloc (sizeof (*cf));
    struct passwd *pw = getpwuid (getuid ());
    char *confdir;

    if (!pw || !pw->pw_dir || strlen (pw->pw_dir) == 0) {
        free (cf);
        return NULL;
    }
    if (!(cf->z = zconfig_new ("root", NULL)))
        oom ();
    if (!(cf->environment = zhash_new()))
        oom ();

    initialize_environment(cf);

    if(!(confdir = xasprintf ("%s/.flux", pw->pw_dir)))
        oom ();
    flux_conf_set_directory(cf, confdir);
    free(confdir);
    return cf;
}

static void initialize_environment(flux_conf_t cf)
{
    // Defaults from the environment
    flux_conf_environment_from_env (cf, "LUA_CPATH",  ";;", ";"); /* Lua replaces ;; with the default path */
    flux_conf_environment_from_env (cf, "LUA_PATH",   ";;", ";"); /* use a null separator to keep it intact */
    flux_conf_environment_from_env (cf, "PYTHONPATH", "", ":");

    // Build paths
    flux_conf_environment_set (cf, "FLUX_CONNECTOR_PATH", CONNECTOR_PATH,      ":");
    flux_conf_environment_set (cf, "FLUX_EXEC_PATH",      EXEC_PATH,           ":");
    flux_conf_environment_set (cf, "FLUX_MODULE_PATH",    MODULE_PATH,         ":");
    flux_conf_environment_push (cf, "LUA_CPATH",          LUA_CPATH_ADD); /* Lua replaces ;; with the default path */
    flux_conf_environment_push (cf, "LUA_PATH",           LUA_PATH_ADD);
    flux_conf_environment_push (cf, "PYTHONPATH",         PYTHON_PATH);
}

const char *flux_conf_get_directory (flux_conf_t cf)
{
    return cf->confdir;
}

void flux_conf_set_directory (flux_conf_t cf, const char *path)
{
    free (cf->confdir);
    flux_conf_environment_set(cf, "FLUX_CONF_DIRECTORY", path, "");
    cf->confdir = xstrdup (path);
}

void flux_conf_clear (flux_conf_t cf)
{
    zconfig_destroy (&cf->z);
    if (!(cf->z = zconfig_new ("root", NULL)))
        oom ();
    zhash_destroy (&cf->environment);
    if (!(cf->environment = zhash_new()))
        oom ();
    initialize_environment (cf);
}

int flux_conf_load (flux_conf_t cf)
{
    char *path = xasprintf ("%s/config", cf->confdir);
    int rc = -1;

    if (access (path, R_OK) < 0)
        goto done;
    zconfig_destroy (&cf->z);
    if (!(cf->z = zconfig_load (path))) {
        errno = EINVAL; /* FIXME more appropriate 'parse error' errno? */
        goto done;
    }

    rc = 0;
done:
    free (path);
    return rc;
}

int flux_conf_save (flux_conf_t cf)
{
    struct stat sb;

    if (stat (cf->confdir, &sb) < 0)
        return -1;
    if (!S_ISDIR (sb.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    char *path = xasprintf ("%s/config", cf->confdir);

    zconfig_set_comment (cf->z, NULL);
    zconfig_set_comment (cf->z, " The format of this file is described in");
    zconfig_set_comment (cf->z, "     http://rfc.zeromq.org/spec:4/ZPL");
    zconfig_set_comment (cf->z, " NOTE: indents must be exactly 4 spaces");
    zconfig_set_comment (cf->z, "");

    umask (022);
    if (zconfig_save (cf->z, path) < 0) {
        free (path);
        return -1;
    }
    free (path);
    return 0;
}

const char *flux_conf_get (flux_conf_t cf, const char *key)
{
    char *val = NULL;
    char *nkey = NULL;
    zconfig_t *z;

    if (!key || strchr (key, '/')) {
        errno = EINVAL;
        goto done;
    }
    nkey = xstrsub (key, '.', '/');
    z = zconfig_locate (cf->z, nkey);
    if (!z || !(val = zconfig_value (z)) || strlen (val) == 0) {
        val = NULL;
        errno = ENOENT;
        goto done;
    }
done:
    if (nkey)
        free (nkey);
    return val;
}

int flux_conf_put (flux_conf_t cf, const char *key, const char *fmt, ...)
{
    char *val = NULL;
    char *nkey = NULL;
    va_list ap;
    zconfig_t *z;
    int rc = -1;

    if (!key || strchr (key, '/')) {
        errno = EINVAL;
        goto done;
    }
    nkey = xstrsub (key, '.', '/');
    z = zconfig_locate (cf->z, nkey);
    if (z && zconfig_child (z)) {
        errno = EISDIR;
        goto done;
    }
    if (fmt) {
        va_start (ap, fmt);
        val = xvasprintf (fmt, ap);
        va_end (ap);
    }
    if (z)
        zconfig_set_value (z, val ? "%s" : NULL, val);
    else
        zconfig_put (cf->z, nkey, val);
    rc = 0;
done:
    if (nkey)
        free (nkey);
    if (val)
        free (val);
    return rc;
}

void flux_conf_itr_destroy (flux_conf_itr_t itr)
{
    char *item;
    while ((item = zlist_pop (itr->zl)))
        free (item);
    zlist_destroy (&itr->zl);
    free (itr);
}

static void zconfig_to_zlist (zconfig_t *z, const char *prefix, zlist_t *zl)
{
    z = zconfig_child (z);
    while (z) {
        char *key = xasprintf ("%s%s%s", prefix ? prefix : "",
                                         prefix ? "."    : "",
                                         zconfig_name (z));
        if (zconfig_child (z)) {
            zconfig_to_zlist (z, key, zl);
            free (key);
        } else {
            if (zlist_append (zl, key) < 0)
                oom ();
        }
        z = zconfig_next (z);
    }
}

flux_conf_itr_t flux_conf_itr_create (flux_conf_t cf)
{
    flux_conf_itr_t itr = xzmalloc (sizeof (*itr));
    itr->cf = cf;
    if (!(itr->zl = zlist_new ()))
        oom ();
    zconfig_to_zlist (cf->z, NULL, itr->zl);
    itr->item = zlist_first (itr->zl);
    return itr;
}

const char *flux_conf_next (flux_conf_itr_t itr)
{
    const char *item = itr->item;
    if (item)
        itr->item = zlist_next (itr->zl);
    return item;
}

static const char * flux_conf_environment_get_inner (struct env_item *item)
{
    if (! item)
        return NULL;

    if (item->components.length == 1)
        return item->components.data[0];

    if (! item->clean) {
        sdsfree(item->string_cache);
        item->string_cache = sdsjoinsds(item->components.data, item->components.length, item->sep, sdslen(item->sep));
    }

    return item->string_cache;
}

const char * flux_conf_environment_get (flux_conf_t cf, const char *key)
{
    struct env_item *item = zhash_lookup(cf->environment, key);
    return flux_conf_environment_get_inner(item);
}

static void split_and_push(struct env_item * item, const char *value, bool before)
{
    int value_count=0, i, j;
    bool insert_on_blank = true;

    if (!value || strlen(value) == 0)
        return;

    sds * new_parts = sdssplitlen(value, strlen(value), item->sep, strlen(item->sep), &value_count);

    for (i=0; i<value_count; i++){
        const char *old_path_part;
        bool skip = false;
        vec_foreach(&item->components, old_path_part, j){
            if ( !strcmp(new_parts[i], old_path_part) ) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            if (!sdslen(new_parts[i]) && insert_on_blank) {
                // This odd dance is for lua's default path item
                new_parts[i] = sdscpy(new_parts[i], item->sep);
                insert_on_blank=false;
            }
            if (before)
                vec_insert(&item->components, 0, new_parts[i]);
            else
                vec_push(&item->components, new_parts[i]);
            item->clean = false;
            new_parts[i] = NULL; // Prevent this sds from being freed by sdsfreesplitres
        }
    }

    sdsfreesplitres(new_parts, value_count);
}

static struct env_item* env_item_new(const char *separator)
{
    struct env_item * item = xzmalloc(sizeof(struct env_item));
    item->sep = sdsnew(separator);
    item->string_cache = sdsempty();
    vec_init(&item->components);
    return item;
}

static void env_item_destroy(void *input)
{
    struct env_item *item = (struct env_item*) input;
    vec_deinit(&item->components);
    sdsfree(item->sep);
    sdsfree(item->string_cache);
    free(item);
}

static void flux_conf_environment_set_inner (flux_conf_t cf, const char *key,
                             const sds value, const char *separator)
{
    struct env_item * item = env_item_new(separator);
    item->clean = false;
    if (sdslen(item->sep))
        split_and_push(item, value, false);
    else
        vec_push(&item->components, value);
    zhash_update(cf->environment, key, (void *)item);
    zhash_freefn(cf->environment, key, (zhash_free_fn*)env_item_destroy);
}

void flux_conf_environment_set (flux_conf_t cf, const char *key,
                             const char *value, const char *separator)
{
    flux_conf_environment_set_inner (cf, key, sdsnew(value), separator);
}

void flux_conf_environment_unset (flux_conf_t cf, const char *key)
{
    flux_conf_environment_set_inner (cf, key, sdsempty(), "");
}

static void flux_conf_environment_push_inner (flux_conf_t cf, const char *key,
                              const char *value, bool before)
{
    if (!value || strlen(value) == 0)
        return;

    struct env_item * item = zhash_lookup(cf->environment, key);
    if (!item)
        item = env_item_new("");

    split_and_push(item, value, before);
}

void flux_conf_environment_push (flux_conf_t cf, const char *key,
                              const char *value)
{
    flux_conf_environment_push_inner(cf, key, value, true);
}

void flux_conf_environment_push_back (flux_conf_t cf, const char *key,
                                   const char *value)
{
    flux_conf_environment_push_inner(cf, key, value, false);
}

void flux_conf_environment_from_env(flux_conf_t cf, const char *key, const
        char *default_base, const char *separator)
{
    const char * env = getenv(key);
    if (! env)
        env = default_base;
    flux_conf_environment_set(cf, key, env, separator);
}

void flux_conf_environment_set_separator(flux_conf_t cf, const char *key,
        const char *separator)
{
    struct env_item * item = zhash_lookup(cf->environment, key);
    if (item) {
        item->sep = sdscpy(item->sep, separator);
    }
}

const char *flux_conf_environment_first (flux_conf_t cf)
{
    return flux_conf_environment_get_inner(zhash_first(cf->environment));
}

const char *flux_conf_environment_next (flux_conf_t cf)
{
    return flux_conf_environment_get_inner(zhash_next(cf->environment));
}

const char *flux_conf_environment_cursor (flux_conf_t cf)
{
    return zhash_cursor(cf->environment);
}

void flux_conf_environment_apply(flux_conf_t cf)
{
    const char *key, *value;
    struct env_item *item;
    FOREACH_ZHASH(cf->environment, key, item) {
        value = flux_conf_environment_get_inner(item);
        if ( sdslen((sds)value) ){
            if (setenv (key, value, 1) < 0)
                err_exit ("setenv: %s=%s", key, value);
        } else {
            if(unsetenv(key))
                err_exit ("unsetenv: %s=%s", key, value);
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
