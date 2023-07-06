/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* priority-default.c - builtin default priority plugin.
 *
 * Simply sets priority to current urgency.
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/jobtap.h>

/*  The current implementation of priority.get just copies
 *   the urgency to the priority.
 */
static int priority_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    int urgency = -1;
    flux_t *h = flux_jobtap_get_flux (p);
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "urgency", &urgency) < 0) {
        flux_log (h, LOG_ERR,
                 "flux_plugin_arg_unpack: %s",
                 flux_plugin_arg_strerror (args));
        return -1;
    }
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                             "{s:i}",
                             "priority", urgency) < 0) {
        flux_log (h, LOG_ERR,
                 "flux_plugin_arg_pack: %s",
                 flux_plugin_arg_strerror (args));
        return -1;
    }
    return 0;
}

int priority_default_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_add_handler (p,
                                 "job.state.priority",
                                 priority_cb,
                                 NULL) < 0
        || flux_plugin_add_handler (p,
                                    "job.priority.get",
                                    priority_cb,
                                    NULL) < 0) {
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

