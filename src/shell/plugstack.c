/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <glob.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/iterators.h"

#include "plugstack.h"

#ifdef PLUGSTACK_STANDALONE
#undef  shell_log_error
#undef  shell_log_errno
#define shell_log_error(...) fprintf (stderr, __VA_ARGS__)
#define shell_log_errno(...) fprintf (stderr, __VA_ARGS__)
#endif

struct plugstack {
    char *searchpath;   /* If set, search path for plugstack_load()        */
    zhashx_t *aux;      /* aux items to propagate to loaded plugins        */
    zlistx_t *plugins;  /* Ordered list of loaded plugins                  */
    zhashx_t *names;    /* Hash for lookup of plugins by name              */
};

void plugstack_unload_name (struct plugstack *st, const char *name)
{
    void *item;
    if ((item = zhashx_lookup (st->names, name))) {
        zlistx_delete (st->plugins, item);
        zhashx_delete (st->names, name);
    }
}

int plugstack_set_searchpath (struct plugstack *st, const char *path)
{
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    free (st->searchpath);
    st->searchpath = NULL;
    if (path && !(st->searchpath = strdup (path)))
        return -1;
    return 0;
}

const char *plugstack_get_searchpath (struct plugstack *st)
{
    if (!st) {
        errno = EINVAL;
        return NULL;
    }
    return st->searchpath;
}

int plugstack_plugin_aux_set (struct plugstack *st,
                              const char *name,
                              void *data)
{
    if (!st || !name) {
        errno = EINVAL;
        return -1;
    }
    zhashx_update (st->aux, name, data);
    return 0;
}

int plugstack_push (struct plugstack *st, flux_plugin_t *p)
{
    const char *name;
    void *item;

    if (!st || !p || !(name = flux_plugin_get_name (p))) {
        errno = EINVAL;
        return -1;
    }
    if (!(item = zlistx_add_end (st->plugins, p)))
        return -1;

    /* Override any existing plugin with the same name */
    plugstack_unload_name (st, name);

    if (zhashx_insert (st->names, name, item) < 0)
        shell_log_error ("failed to register plugin as name=%s", name);
    return 0;
}

void plugstack_destroy (struct plugstack *st)
{
    if (st) {
        int saved_errno = errno;
        zlistx_destroy (&st->plugins);
        zhashx_destroy (&st->names);
        zhashx_destroy (&st->aux);
        free (st->searchpath);
        free (st);
        errno = saved_errno;
    }
}

static void plugin_destroy (flux_plugin_t **pp)
{
    if (pp) {
        flux_plugin_destroy (*pp);
        *pp = NULL;
    }
}

struct plugstack * plugstack_create (void)
{
    struct plugstack *st = calloc (1, sizeof (*st));
    if (!st
        || !(st->plugins = zlistx_new ())
        || !(st->names = zhashx_new ())
        || !(st->aux = zhashx_new ())) {
        plugstack_destroy (st);
        return NULL;
    }
    zlistx_set_destructor (st->plugins, (zlistx_destructor_fn *) plugin_destroy);
    return (st);
}

/*  Copy the plugin list, unsetting the destructor so plugins aren't
 *   destroyed on destruction of the list copy.
 */
static zlistx_t *plugstack_list_dup (struct plugstack *st)
{
    zlistx_t *l = zlistx_dup (st->plugins);
    if (l)
        zlistx_set_destructor (l, NULL);
    return l;
}

int plugstack_call (struct plugstack *st,
                    const char *name,
                    flux_plugin_arg_t *args)
{
    int rc = 0;
    flux_plugin_t *p = NULL;

    /* Duplicate list to make plugstack_call() reentrant.
     */
    zlistx_t *l = plugstack_list_dup (st);
    if (!l)
        return -1;

    p = zlistx_first (l);
    while (p) {
        if (flux_plugin_call (p, name, args) < 0) {
            shell_log_error ("plugin '%s': %s failed",
                             flux_plugin_get_name (p),
                             name);
            rc = -1;
        }
        p = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return rc;
}

static int plugin_aux_from_zhashx (flux_plugin_t *p, zhashx_t *aux)
{
    const char *key;
    void *val;
    FOREACH_ZHASHX (aux, key, val) {
        if (flux_plugin_aux_set (p, key, val, NULL) < 0)
            return -1;
    }
    return 0;
}

static int load_plugin (struct plugstack *st,
                        const char *path,
                        const char *conf)
{
    flux_plugin_t *p = flux_plugin_create ();
    if (!p)
        return -1;
    if (conf && flux_plugin_set_conf (p, conf) < 0) {
        shell_log_error ("set_conf: %s: %s", path, flux_plugin_strerror (p));
        goto error;
    }
    if (plugin_aux_from_zhashx (p, st->aux) < 0) {
        shell_log_error ("%s: failed to set aux items", path);
    }
    if (flux_plugin_load_dso (p, path) < 0) {
        shell_log_error ("%s", flux_plugin_strerror (p));
        goto error;
    }
    if (plugstack_push (st, p) < 0) {
        shell_log_errno ("plugstack_push (%s)", path);
        goto error;
    }
    return 0;
error:
    flux_plugin_destroy (p);
    return -1;
}

static int load_from_glob (struct plugstack *st, glob_t *gl, const char *conf)
{
    int n = 0;
    size_t i;
    for (i = 0; i < gl->gl_pathc; i++) {
        if (load_plugin (st, gl->gl_pathv[i], conf) < 0)
            return -1;
        n++;
    }
    return n;
}

static int plugstack_glob (struct plugstack *st,
                           const char *pattern,
                           const char *conf)
{
    glob_t gl;
    int rc = -1;
    int flags = 0;

#ifdef GLOB_TILDE_CHECK
    flags |= GLOB_TILDE_CHECK;
#endif

    rc = glob (pattern, flags, NULL, &gl);
    switch (rc) {
        case 0:
            rc = load_from_glob (st, &gl, conf);
            break;
        case GLOB_NOMATCH:
            rc = 0;
            break;
        case GLOB_NOSPACE:
            shell_log_error ("glob: Out of memory");
            break;
        case GLOB_ABORTED:
            //log_err ("glob: failed to read %s", pattern);
            break;
        default:
            shell_log_error ("glob: unknown rc = %d", rc);
    }
    globfree (&gl);
    return rc;
}

/*  Return 1 if either searchpath is NULL, or pattern starts with '/' or '~'.
 *  Also, pattern starting with './' to explicitly bypass searchpath.
 */
static int no_searchpath (const char *searchpath, const char *pattern)
{
    return (!searchpath
            || pattern[0] == '/'
            || pattern[0] == '~'
            || (pattern[0] == '.' && pattern[1] == '/'));
}

static void item_free (void **item)
{
    if (*item) {
        free (*item);
        *item = NULL;
    }
}

static zlistx_t * list_o_patterns (const char *searchpath, const char *pattern)
{
    char *copy;
    char *str;
    char *dir;
    char *path;
    char *sp = NULL;
    zlistx_t *l = zlistx_new ();

    if (!l || !(copy = strdup (searchpath)))
        return NULL;
    str = copy;

    zlistx_set_destructor (l, item_free);

    while ((dir = strtok_r (str, ":", &sp))) {
        if (asprintf (&path, "%s/%s", dir, pattern) < 0)
            goto error;
        zlistx_add_end (l, path);
        str = NULL;
    }
    free (copy);
    return l;
error:
    free (copy);
    zlistx_destroy (&l);
    return NULL;
}

int plugstack_load (struct plugstack *st,
                    const char *pattern,
                    const char *conf)
{
    zlistx_t *l;
    char *path;
    int rc = 0;

    if (!st || !pattern) {
        errno = EINVAL;
        return -1;
    }

    if (no_searchpath (st->searchpath, pattern))
        return plugstack_glob (st, pattern, conf);

    if (!(l = list_o_patterns (st->searchpath, pattern)))
        return -1;
    /*
     *  NB: traverse searchpath list in *reverse* order. this is because
     *   of the way plugstack works, the _last_ loaded plugin takes
     *   precedence. So to preserve the idiom of "searchpath" we iterate
     *   the search in reverse order.
     */
    path = zlistx_last (l);
    while (path) {
        int n;
        if ((n = plugstack_glob (st, path, conf)) < 0)
            return -1;
        rc += n;
        path = zlistx_prev (l);
    }
    zlistx_destroy (&l);
    return rc;
}

/* vi: ts=4 sw=4 expandtab
 */

