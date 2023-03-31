/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queues.c - simple abstraction of queues in flux
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <jansson.h>

#include "top.h"

struct queues {
    json_t *flux_config;
    char *queue_name;
    json_t *queue_constraint;
};

void queues_destroy (struct queues *queues)
{
    if (queues) {
        int saved_errno = errno;
        json_decref (queues->flux_config);
        free (queues->queue_name);
        json_decref (queues->queue_constraint);
        free (queues);
        errno = saved_errno;
    }
}

struct queues *queues_create (json_t *flux_config)
{
    struct queues *queues;

    if (!(queues = calloc (1, sizeof (*queues))))
        return NULL;
    queues->flux_config = json_incref (flux_config);
    return queues;
}

static bool is_valid_queue (struct queues *queues, const char *name)
{
    json_t *tmp;

    if (json_unpack (queues->flux_config,
                     "{s:{s:o}}",
                     "queues", name, &tmp) < 0)
        return false;
    return true;
}

void queues_set_queue (struct queues *queues, const char *name)
{
    json_t *requires = NULL;

    assert (name);

    /* first verify queue legit */
    if (!is_valid_queue (queues, name))
        fatal (0, "queue %s not configured", name);

    if (!(queues->queue_name = strdup (name)))
        fatal (0, "cannot copy queue name");

    /* not required to be configured */
    (void) json_unpack (queues->flux_config,
                        "{s:{s:{s:o}}}",
                        "queues",
                        name,
                        "requires",
                        &requires);
    if (requires) {
        if (!(queues->queue_constraint = json_pack ("{s:O}",
                                                    "properties",
                                                    requires)))
            fatal (0, "Error creating queue constraints");
    }
}

void queues_get_queue_name (struct queues *queues, const char **name)
{
    if (name)
        (*name) = queues->queue_name;
}

void queues_get_queue_constraint (struct queues *queues, json_t **constraint)
{
    if (constraint)
        (*constraint) = queues->queue_constraint;
}

// vi:ts=4 sw=4 expandtab
