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
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

/*  plugstack test plugin.
 *
 *  Set callback "result" to TEST_PLUGIN_RESULT set at
 *   build time via CFLAGS. Allows a single source file to create
 *   multiple plugins of the same name but different callbacks.
 */

static int callback (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    char *test = flux_plugin_aux_get (p, "test");
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT,
                                "{s:s s:s?}",
                                "result", TEST_PLUGIN_RESULT,
                                "aux", test);
}

int flux_plugin_init (flux_plugin_t *p)
{
    /*  All plugins have the same name to test the "last loaded wins"
     *   property of the plugstack.
     */
    if (flux_plugin_set_name (p, "test") < 0)
        return -1;
    return (flux_plugin_add_handler (p, "test.*", callback, NULL));
}

/*
 * vi:ts=4 sw=4 expandtab
 */
