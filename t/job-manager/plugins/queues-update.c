/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queues-update.c - test the queues.update callback
 *
 * Asserts the delivery contract: the job manager always hands the plugin a
 * configuration object with a "queues" array, never a JSON null and never a
 * missing key. "No queues configured" is an object with an empty array.
 *
 * The unpack below uses "s:o" rather than "s?o" deliberately, so a missing
 * key fails here rather than downstream. A JSON null would satisfy that
 * unpack, so it is rejected explicitly by the json_is_object() check.
 *
 * The plugin also rejects any configuration containing a queue named
 * "rejectme". Rejecting on queue name rather than on an invalid config is
 * what makes the rejection path reachable from a test:
 * conf_policy_validate() rejects an invalid config before any plugin is
 * consulted.
 *
 * This plugin is loaded at runtime, so it is ordered after the builtin
 * limit-* plugins. The limits a builtin enforces after a rejection show
 * whether it was rolled back.
 */

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"

static bool has_queue (json_t *queues, const char *name)
{
    size_t index;
    json_t *entry;

    json_array_foreach (queues, index, entry) {
        const char *s;
        if (json_unpack (entry, "{s:s}", "name", &s) == 0
            && streq (s, name))
            return true;
    }
    return false;
}

static int queues_update_cb (flux_plugin_t *p,
                             const char *topic,
                             flux_plugin_arg_t *args,
                             void *arg)
{
    json_t *conf = NULL;
    json_t *queues;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:o}",
                                "queues", &conf) < 0)
        return flux_jobtap_error (p,
                                  args,
                                  "queues-update: no queues key: %s",
                                  flux_plugin_arg_strerror (args));
    if (!json_is_object (conf))
        return flux_jobtap_error (p,
                                  args,
                                  "queues-update: conf is %s, expected"
                                  " an object",
                                  json_is_null (conf) ? "null" : "not an"
                                                                 " object");
    if (!(queues = json_object_get (conf, "queues"))
        || !json_is_array (queues))
        return flux_jobtap_error (p,
                                  args,
                                  "queues-update: conf.queues is not an"
                                  " array");
    if (has_queue (queues, "rejectme"))
        return flux_jobtap_error (p,
                                  args,
                                  "queues-update: rejecting this config");
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "queues.update", queues_update_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
