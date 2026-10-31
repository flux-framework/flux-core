/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bad-event.c - post an event that cannot be applied
 *
 * In job.state.depend, post set-flags events with an invalid flag and
 * a non-string flag, followed by a valid test-event. All posts are
 * queued behind the current event, so all appear to succeed.
 */

#include <flux/jobtap.h>

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    if (flux_jobtap_event_post_pack (p,
                                     FLUX_JOBTAP_CURRENT_JOB,
                                     "set-flags",
                                     "{s:[ss]}",
                                     "flags", "debug", "notaflag") < 0
        || flux_jobtap_event_post_pack (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                        "set-flags",
                                        "{s:[i]}",
                                        "flags", 42) < 0
        || flux_jobtap_event_post_pack (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                        "test-event",
                                        NULL) < 0) {
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "bad-event",
                                            0,
                                            "failed to post events");
    }
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "job.state.depend", depend_cb, NULL);
}

// vi:ts=4 sw=4 expandtab
