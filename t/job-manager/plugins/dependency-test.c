/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dependency-test.c - keep jobs in depend state and wait for
 *  an RPC to release
 */

#include <jansson.h>

#include <flux/core.h>
#include <flux/jobtap.h>


static void remove_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    flux_jobid_t id;
    const char *description = NULL;
    flux_plugin_t *p = arg;

    if (flux_request_unpack (msg, NULL,
                             "{s:I s:s}",
                             "id", &id,
                             "description", &description) < 0) {
        flux_log_error (h, "failed to unpack dependency-test.remove msg");
        goto error;
    }
    if (flux_jobtap_dependency_remove (p, id, description) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "flux_respond");
    return;
error:
    flux_respond_error (h, msg, errno, flux_msg_last_error (msg));
}

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    flux_jobid_t id;
    const char *name = NULL;
    int remove = 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:{s:s s?i}}",
                                "id", &id,
                                "dependency",
                                "value", &name,
                                "remove", &remove) < 0) {
        return flux_jobtap_reject_job (p, args,
                                       "failed to unpack dependency args: %s",
                                       flux_plugin_arg_strerror (args));
    }

    if (flux_jobtap_dependency_add (p, id, name) < 0) {
        flux_log_error (flux_jobtap_get_flux (p),
                        "flux_jobtap_dependency_add (%s)",
                        name);
        return -1;
    }
    if (remove && flux_jobtap_dependency_remove (p, id, name) < 0) {
        flux_log_error (flux_jobtap_get_flux (p),
                        "flux_jobtap_dependency_remove (%s)",
                        name);
        return -1;
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.dependency.test", depend_cb, NULL },
    { 0 },
};

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, "dependency-test", tab) < 0
        || flux_jobtap_service_register (p, "remove", remove_cb, p) < 0)
        return -1;
    return 0;
}
