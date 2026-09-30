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
 */

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

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
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "queues.update", queues_update_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
