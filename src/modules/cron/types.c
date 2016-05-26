/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
} cron_types[] = {
    { "interval", &cron_interval_operations },
    { "event",    &cron_event_operations    },
    { "datetime", &cron_datetime_operations },
    { NULL, NULL }
};

int cron_type_operations_lookup (const char *name,
    struct cron_entry_ops *ops)
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
