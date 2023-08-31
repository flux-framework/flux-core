/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* update-test.c - test plugin authorization of job update
 *  allow updates of the 'test' and 'test2' attributes for test
 *  purposes.
 */

#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"

static int update_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    struct flux_msg_cred cred;
    const char *value;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:{s:i s:i}}",
                                "value", &value,
                                "cred",
                                  "userid", &cred.userid,
                                  "rolemask", &cred.rolemask) < 0)
        return flux_jobtap_error (p, args, "plugin args unpack failed");
    if (streq (value, "fail-test"))
        return flux_jobtap_error (p, args, "rejecting update: fail-test");
    return 0;
}

static int job_updated (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    const char *value = NULL;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:{s?s}}",
                                "updates",
                                 "attributes.system.test", &value) < 0)
        return flux_jobtap_error (p, args, "plugin args unpack failed");
    if (value
        && flux_jobtap_event_post_pack (p,
                                        FLUX_JOBTAP_CURRENT_JOB,
                                        "update-test",
                                        "{s:s}",
                                        "value", value) < 0)
        return flux_jobtap_error (p, args, "flux_job_event_post_pack failed");
    return 0;
}


static const struct flux_plugin_handler tab[] = {
    { "job.update", job_updated, NULL },
    { "job.update.attributes.system.test", update_cb, NULL },
    { "job.update.attributes.system.test2", update_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "update-test", tab) < 0)
        return -1;
    return 0;
}

// vi:ts=4 sw=4 expandtab
