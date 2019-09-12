/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>

static int foo (flux_plugin_t *p, const char *topic, flux_plugin_arg_t *args)
{
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "foo");
}

static int bar (flux_plugin_t *p, const char *topic, flux_plugin_arg_t *args)
{
    return flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                                 "{s:s}", "result", "bar");
}

static const struct flux_plugin_handler tab []= {
    { "test.foo", foo },
    { "test.bar", bar },
    { NULL,       NULL }
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "plugin-test", tab) < 0)
        return -1;
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
