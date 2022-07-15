/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* config.c - test conf.update callback
 */

#include <jansson.h>
#include <flux/jobtap.h>

static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    const char *test;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s:{s:s}}}",
                                "conf",
                                  "testconfig",
                                    "testkey", &test) < 0)
        return flux_jobtap_error (p,
                                  args,
                                  "Error parsing [testconfig]: %s",
                                  flux_plugin_arg_strerror (args));
    return 0;
}


int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "conf.update", conf_update_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
