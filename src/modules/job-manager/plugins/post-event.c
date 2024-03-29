/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* post-event.c - post manual events to job eventlog
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

static void post_event_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    flux_plugin_t *p = arg;
    flux_jobid_t id;
    const char *name;
    json_t *context = NULL;

    if (flux_msg_unpack (msg,
                         "{s:I s:s s?o}",
                         "id", &id,
                         "name", &name,
                         "context", &context) < 0)
        goto error;
    if (context) {
        if (flux_jobtap_event_post_pack (p, id, name, "O", context) < 0)
            goto error;
    }
    else if (flux_jobtap_event_post_pack (p, id, name, NULL) < 0)
            goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.post-event");
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.post-event");
}


int post_event_init (flux_plugin_t *p)
{
    if (flux_jobtap_service_register_ex (p, "post", 0, post_event_cb, p) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
