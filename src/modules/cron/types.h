/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_CRON_TYPES_H
#define HAVE_CRON_TYPES_H
#include "entry.h"

/*
 *  Lookup cron entry operations for cron entry type named "name"
 */
int cron_type_operations_lookup (const char *name, struct cron_entry_ops *ops);

#endif /* !HAVE_CRON_TYPES_H */
