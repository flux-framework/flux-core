/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

int mod_main (flux_t *h, int argc, char *argv[])
{
    flux_msg_t *msg;
    struct flux_match match;

    if (flux_event_subscribe (h, "running.go") < 0)
        return -1;
    if (flux_module_set_running (h) < 0)
        return -1;
    match = FLUX_MATCH_EVENT;
    match.topic_glob = "running.go";
    if (!(msg = flux_recv (h, match, 0))) {
        flux_log_error (h, "flux_recv");
        return -1;
    }
    flux_log (h, LOG_DEBUG, "received event");
    flux_msg_destroy (msg);

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
