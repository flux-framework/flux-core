/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* builtins/random.c - test plugin that randomizes priority every
 *  second.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <flux/core.h>
#include <flux/jobtap.h>

static int priority_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    int64_t priority = lrand48 ();
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                              "{s:I}",
                              "priority", priority) < 0) {
        flux_t *h = flux_jobtap_get_flux (p);
        flux_log (h, LOG_ERR,
                 "flux_plugin_arg_pack: %s",
                 flux_plugin_arg_strerror (args));
        return -1;
    }
    return 0;
}

static void reprioritize (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    flux_jobtap_reprioritize_all (arg);
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_reactor_t *r;
    flux_watcher_t *tw;
    flux_t *h = flux_jobtap_get_flux (p);

    if (flux_plugin_set_name (p, "random") < 0)
        return -1;

    if (!h
        || !(r = flux_get_reactor (h))
        || !(tw = flux_timer_watcher_create (r, 1., 1., reprioritize, p)))
        return -1;

    srand48 (getpid());
    flux_watcher_start (tw);

    /* Auto-destroy timer watcher on plugin exit:
     */
    flux_plugin_aux_set (p, NULL, tw, (flux_free_f) flux_watcher_destroy);

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
