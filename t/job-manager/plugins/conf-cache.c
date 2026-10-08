/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* conf-cache.c - cache the last accepted conf.update value
 *
 * Caches [testconfig] from every conf.update it accepts and returns it from
 * plugin.query, so a test can see which configuration the plugin holds.
 *
 * Load this ahead of a plugin that rejects a conf.update (config.so). The
 * stack walk stops at the first rejection, so this plugin caches the
 * rejected configuration first, and the query shows whether the job manager
 * rolled it back.
 */

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

static json_t *cached;

static void cache_destroy (void *arg)
{
    json_decref (cached);
    cached = NULL;
}

static int conf_update_cb (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *arg)
{
    json_t *testconfig = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s?{s?o}}",
                                "conf",
                                  "testconfig", &testconfig) < 0)
        return flux_jobtap_error (p,
                                  args,
                                  "conf-cache: unpack: %s",
                                  flux_plugin_arg_strerror (args));
    json_decref (cached);
    cached = json_incref (testconfig);
    return 0;
}

static int query_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    return flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_OUT,
                                 "{s:O?}",
                                 "testconfig", cached);
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_add_handler (p, "conf.update", conf_update_cb, NULL) < 0
        || flux_plugin_add_handler (p, "plugin.query", query_cb, NULL) < 0
        || flux_plugin_aux_set (p, NULL, (void *)1, cache_destroy) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
