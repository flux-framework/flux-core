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
    int count;                  /* number of configured (named) queues */
    struct list_head queues_list;
    struct queue *current;
};

/* Create a queue from its [queues.NAME] config entry.  'name' NULL (with
 * 'entry' NULL too) means the synthetic "all" queue, which has no
 * constraint.
 */
static struct queue *queue_create (const char *name, json_t *entry)
{
    struct queue *q = calloc (1, sizeof (*q));
    if (!q)
        fatal (0, "could not allocate queue entry");

    if (name) {
        json_t *requires = NULL;

        if (!(q->name = strdup (name)))
            fatal (0, "could not duplicate queue name entry");

        /* not required to be configured */
        (void) json_unpack (entry, "{s?o}", "requires", &requires);
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
        while ((q = list_pop (&queues->queues_list,
                              struct queue,
                              list_node)))
            queue_destroy (q);
        free (queues);
        errno = saved_errno;
    }
}

/* Build the queue list from the broker config 'flux_config'.  Nothing from
 * it is retained: each queue keeps only the constraint built here.
 */
static void queues_list_setup (struct queues *queues, json_t *flux_config)
{
    struct queue *q;
    json_t *o;
    const char *name;
    json_t *value;

    list_head_init (&queues->queues_list);

    /* first, we add a queue for "all" queues with NULL queue name. */
    q = queue_create (NULL, NULL);
    list_add_tail (&queues->queues_list, &q->list_node);

    /* return if no queues configured */
    if (json_unpack (flux_config, "{s:o}", "queues", &o) < 0)
        return;

    json_object_foreach (o, name, value) {
        q = queue_create (name, value);
        list_add_tail (&queues->queues_list, &q->list_node);
        queues->count++;
    }
}

struct queues *queues_create (json_t *flux_config)
{
    struct queues *queues;

    if (!(queues = calloc (1, sizeof (*queues))))
        return NULL;

    queues_list_setup (queues, flux_config);

    /* must work, minimally the "all" queue is configured */
    queues->current = list_top (&queues->queues_list, struct queue, list_node);
    return queues;
}

bool queues_configured (struct queues *queues)
{
    return queues->count > 0;
}

void queues_set_queue (struct queues *queues, const char *name)
{
    struct queue *q = NULL;

    assert (name);

    list_for_each(&queues->queues_list, q, list_node) {
        /* q->name == NULL is "all" queues, don't compare */
        if (q->name && streq (q->name, name)) {
            queues->current = q;
            return;
        }
    }
    fatal (0, "queue %s not configured", name);
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
