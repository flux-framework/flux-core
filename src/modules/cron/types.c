/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include "entry.h"

/* List of external operations structures defined in
 *  type-specific source files:
 */
extern struct cron_entry_ops cron_interval_operations;
extern struct cron_entry_ops cron_event_operations;
extern struct cron_entry_ops cron_datetime_operations;

static struct cron_typeinfo {
    const char *name;
    struct cron_entry_ops *ops;
} cron_types[] = {{"interval", &cron_interval_operations},
                  {"event", &cron_event_operations},
                  {"datetime", &cron_datetime_operations},
                  {NULL, NULL}};

int cron_type_operations_lookup (const char *name, struct cron_entry_ops *ops)
{
    struct cron_typeinfo *type = cron_types;
    while (type && type->name) {
        if (strcmp (name, type->name) == 0) {
            *ops = *type->ops;
            return (0);
        }
        type++;
    }
    errno = ENOENT;
    return (-1);
}
