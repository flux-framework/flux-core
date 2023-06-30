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

#include "ccan/list/list.h"
#include "src/common/libccan/ccan/str/str.h"

#include "top.h"

struct queue {
    char *name;
    json_t *constraint;
    struct list_node list_node;
};

struct queues {
    json_t *flux_config;
    struct list_head queues_list;
    struct queue *current;
};

static struct queue *queue_create (struct queues *queues, const char *name)
{
    json_t *requires = NULL;
    struct queue *q = calloc (1, sizeof (*q));
    if (!q)
        fatal (0, "could not allocate queue entry");

    /* name == NULL means "all" queues */
    if (name) {
        if (!(q->name = strdup (name)))
            fatal (0, "could not duplicate queue name entry");

        /* not required to be configured */
        (void) json_unpack (queues->flux_config,
                            "{s:{s:{s:o}}}",
                            "queues",
                            name,
                            "requires",
                            &requires);
        if (requires) {
            if (!(q->constraint = json_pack ("{s:O}",
                                             "properties",
                                             requires)))
                fatal (0, "could not allocate queue constraint");
        }
    }

    list_node_init (&q->list_node);
    return q;
}

static void queue_destroy (void *data)
{
    if (data) {
        int save_errno = errno;
        struct queue *q = data;
        free (q->name);
        json_decref (q->constraint);
        free (q);
        errno = save_errno;
    }
}

void queues_destroy (struct queues *queues)
{
    if (queues) {
        int saved_errno = errno;
        struct queue *q;
        json_decref (queues->flux_config);
        while ((q = list_pop (&queues->queues_list,
                              struct queue,
                              list_node)))
            queue_destroy (q);
        free (queues);
        errno = saved_errno;
    }
}

static void queues_list_setup (struct queues *queues)
{
    struct queue *q;
    json_t *o;
    const char *name;
    json_t *value;

    list_head_init (&queues->queues_list);

    /* first, we add a queue for "all" queues with NULL queue name. */
    q = queue_create (queues, NULL);
    list_add_tail (&queues->queues_list, &q->list_node);

    /* return if no queues configured */
    if (json_unpack (queues->flux_config,
                     "{s:o}",
                     "queues", &o) < 0)
        return;

    json_object_foreach (o, name, value) {
        q = queue_create (queues, name);
        list_add_tail (&queues->queues_list, &q->list_node);
    }
}

struct queues *queues_create (json_t *flux_config)
{
    struct queues *queues;

    if (!(queues = calloc (1, sizeof (*queues))))
        return NULL;
    queues->flux_config = json_incref (flux_config);

    queues_list_setup (queues);

    /* must work, minimally the "all" queue is configured */
    queues->current = list_top (&queues->queues_list, struct queue, list_node);
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

bool queues_configured (struct queues *queues)
{
    json_t *tmp;

    if (json_unpack (queues->flux_config,
                     "{s:o}",
                     "queues", &tmp) < 0)
        return false;
    return true;
}

void queues_set_queue (struct queues *queues, const char *name)
{
    struct queue *q = NULL;

    assert (name);

    /* first verify queue legit */
    if (!is_valid_queue (queues, name))
        fatal (0, "queue %s not configured", name);

    list_for_each(&queues->queues_list, q, list_node) {
        /* q->name == NULL is "all" queues, don't compare */
        if (q->name && streq (q->name, name)) {
            queues->current = q;
            break;
        }
    }
}

void queues_next (struct queues *queues)
{
    queues->current = list_next (&queues->queues_list,
                                 queues->current,
                                 list_node);
    if (!queues->current)
        queues->current = list_top (&queues->queues_list,
                                    struct queue,
                                    list_node);
}

void queues_prev (struct queues *queues)
{
    queues->current = list_prev (&queues->queues_list,
                                 queues->current,
                                 list_node);
    if (!queues->current)
        queues->current = list_tail (&queues->queues_list,
                                     struct queue,
                                     list_node);
}

void queues_get_queue_name (struct queues *queues, const char **name)
{
    if (name)
        (*name) = queues->current->name;
}

void queues_get_queue_constraint (struct queues *queues, json_t **constraint)
{
    if (constraint)
        (*constraint) = queues->current->constraint;
}

// vi:ts=4 sw=4 expandtab
