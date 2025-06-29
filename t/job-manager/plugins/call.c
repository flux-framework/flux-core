/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobtap_call testing
 */

#include <errno.h>
#include <string.h>
#include <flux/jobtap.h>

static int depend_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    int result;
    if (flux_jobtap_call (p, FLUX_JOBTAP_CURRENT_JOB, "test.topic", args) < 0)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "jobtap_call: %s",
                                            strerror (errno));
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:i}",
                                "test", &result) < 0)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "flux_plugin_arg_unpack: %s (errno=%d)",
                                            flux_plugin_arg_strerror (args),
                                            errno);
    if (result != 42)
        return flux_jobtap_raise_exception (p,
                                            FLUX_JOBTAP_CURRENT_JOB,
                                            "test",
                                            0,
                                            "expected result=42, got %d",
                                            result);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "job.state.depend",
                                     depend_cb,
                                     NULL);
}

// vi:ts=4 sw=4 expandtab
