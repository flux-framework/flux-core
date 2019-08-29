/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <glob.h>
#include <dlfcn.h>
#include <czmq.h>
#include <stdarg.h>

#include "src/common/libutil/log.h"

#include "plugstack.h"

struct splugin {
    char *path;         /* Path from which plugin was loaded or "builtin"  */
    char *name;         /* Optional name indicating service added to shell */
    zhashx_t *symbols;  /* hash of symbols provided by this plugin         */
    json_t *conf;       /* Optional JSON config for this plugin            */
};

struct plugstack {
    zlistx_t *plugins;  /* Ordered list of loaded plugins                  */
    zhashx_t *names;    /* Hash for lookup of plugins by name              */
};

void splugin_destroy (struct splugin *p)
{
    if (p) {
        int saved_errno = errno;
        json_decref (p->conf);
        zhashx_destroy (&p->symbols);
        free (p->path);
        free (p->name);
        free (p);
        errno = saved_errno;
    }
}

struct splugin * splugin_create (void)
{
    struct splugin *p = calloc (1, sizeof (*p));
    if (!p || !(p->symbols = zhashx_new ())) {
        splugin_destroy (p);
        return NULL;
    }
    return (p);
}

json_t *splugin_conf (struct splugin *p)
{
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return (p->conf);
}

int splugin_set_sym (struct splugin *sp, const char *symbol, void *fn)
{
    if (!sp || !symbol) {
        errno = EINVAL;
        return -1;
    }
    if (fn == NULL) {
        zhashx_delete (sp->symbols, symbol);
        return 0;
    }
    zhashx_update (sp->symbols, symbol, fn);
    return 0;
}

void * splugin_get_sym (struct splugin *sp, const char *symbol)
{
    if (!sp || !symbol) {
        errno = EINVAL;
        return NULL;
    }
    return zhashx_lookup (sp->symbols, symbol);
}

int splugin_set_name (struct splugin *sp, const char *name)
{
    if (!sp || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(sp->name = strdup (name)))
        return -1;
    return 0;
}

void plugstack_unload_name (struct plugstack *st, const char *name)
{
    void *item;
    if ((item = zhashx_lookup (st->names, name))) {
        zlistx_delete (st->plugins, item);
        zhashx_delete (st->names, name);
    }
}

int plugstack_push (struct plugstack *st, struct splugin *p)
{
    void *item;

    if (!st || !p || !p->name) {
        errno = EINVAL;
        return -1;
    }
    if (!(item = zlistx_add_end (st->plugins, p)))
        return -1;

    /* Override any existing plugin with the same name */
    plugstack_unload_name (st, p->name);

    if (zhashx_insert (st->names, p->name, item) < 0)
        log_err ("failed to register plugin as name=%s", p->name);
    return 0;
}

void plugstack_destroy (struct plugstack *st)
{
    if (st) {
        int saved_errno = errno;
        zlistx_destroy (&st->plugins);
        zhashx_destroy (&st->names);
        free (st);
        errno = saved_errno;
    }
}

static void plugin_destroy (struct splugin **pp)
{
    splugin_destroy (*pp);
    *pp = NULL;
}

struct plugstack * plugstack_create (void)
{
    struct plugstack *st = calloc (1, sizeof (*st));
    if (!st
        || !(st->plugins = zlistx_new ())
        || !(st->names = zhashx_new ())) {
        plugstack_destroy (st);
        return NULL;
    }
    zlistx_set_destructor (st->plugins, (czmq_destructor *) plugin_destroy);
    return (st);
}

static int splugin_vcall (struct splugin *sp,
                          const char *name,
                          int nargs,
                          va_list ap)
{
    void *arg1, *arg2;

    void *pfn = splugin_get_sym (sp, name);
    if (!pfn)
        return 0;
    switch (nargs) {
    case 0:
        return (((int (*)()) pfn) ());
        break;
    case 1:
        arg1 = va_arg (ap, void *);
        return (((int (*)(void *)) pfn) (arg1));
        break;
    case 2:
        arg1 = va_arg (ap, void *);
        arg2 = va_arg (ap, void *);
        return (((int (*)(void *, void *)) pfn) (arg1, arg2));
        break;
    }
    errno = EINVAL;
    return -1;
}

int plugstack_call (struct plugstack *st, const char *name, int nargs, ...)
{
    struct splugin *p;
    p = zlistx_first (st->plugins);
    while (p) {
        va_list ap;
        va_start (ap, nargs);
        if (splugin_vcall (p, name, nargs, ap) < 0)
            log_err ("%s: %s failed", p->name, name);
        va_end (ap);
        p = zlistx_next (st->plugins);
    }
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */

