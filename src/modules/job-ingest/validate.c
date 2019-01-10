/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Jobspec validation engine.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#if HAVE_JOBSPEC
#include "jobspec.h"
#endif

#include "validate.h"

flux_future_t *validate_jobspec (flux_t *h, const char *buf, int len)
{
    flux_future_t *f;
    char errbuf[256];
    json_t *o;
    json_error_t error;

    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    flux_future_set_flux (f, h);

    /* Make sure jobspec decodes as JSON.
     * YAML is not allowed here.
     */
    if (!(o = json_loadb (buf, len, 0, &error))) {
        (void)snprintf (errbuf, sizeof (errbuf),
                        "jobspec: invalid JSON: %s", error.text);
        flux_future_fulfill_error (f, EINVAL, errbuf);
        return f;
    }
    json_decref (o);
#if HAVE_JOBSPEC
    /* Call the C++ validator, if configured.
     */
    if (jobspec_validate (buf, len, errbuf, sizeof (errbuf)) < 0) {
        flux_future_fulfill_error (f, EINVAL, errbuf);
        return f;
    }
#endif
    flux_future_fulfill (f, NULL, NULL);
    return f;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
