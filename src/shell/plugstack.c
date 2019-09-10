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

struct plugstack {
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
        log_err ("failed to register plugin as name=%s", name);
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

static void plugin_destroy (flux_plugin_t **pp)
{
    flux_plugin_destroy (*pp);
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

int plugstack_call (struct plugstack *st,
                    const char *name,
                    flux_plugin_arg_t *args)
{
    flux_plugin_t *p = zlistx_first (st->plugins);
    while (p) {
        if (flux_plugin_call (p, name, args) < 0)
            log_err ("%s: %s failed", flux_plugin_get_name (p), name);
        p = zlistx_next (st->plugins);
    }
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */

