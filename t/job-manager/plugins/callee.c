/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobtap_call testing, callee
 */

#include <flux/jobtap.h>

static int test_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *arg)
{
    flux_log (flux_jobtap_get_flux (p), LOG_INFO, "test_cb called");
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT,
                                 "{s:i}",
                                 "test", 42);
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "test.topic", test_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
