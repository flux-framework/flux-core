/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* brokercfg.c - broker configuration
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/log.h"

#include "attr.h"
#include "brokercfg.h"


struct brokercfg {
    flux_t *h;
    char *path;
};

/* Parse config object from TOML config files if path is set;
 * otherwise, create an empty config object.  Store the object
 * in ctx->h for later access by flux_conf_get().
 */
static int brokercfg_parse (flux_t *h, const char *path)
{
    flux_conf_error_t error;
    flux_conf_t *conf;

    if (path) {
        if (!(conf = flux_conf_parse (path, &error))) {
            if (error.lineno == -1)
                log_msg ("Config file error: %s%s%s",
                         error.filename,
                         *error.filename ? ": " : "",
                         error.errbuf);
            else
                log_msg ("Config file error: %s:%d: %s",
                         error.filename,
                         error.lineno,
                         error.errbuf);
            return -1;
        }
    }
    else {
        if (!(conf = flux_conf_create ())) {
            log_err ("Error creating config object");
            return -1;
        }
    }
    if (flux_set_conf (h, conf) < 0) {
        log_err ("Error storing config object in flux_t handle");
        flux_conf_decref (conf);
        return -1;
    }

    return 0;
}

void brokercfg_destroy (struct brokercfg *cfg)
{
    if (cfg) {
        int saved_errno = errno;
        free (cfg->path);
        free (cfg);
        errno = saved_errno;
    }
}

struct brokercfg *brokercfg_create (flux_t *h, const char *path, attr_t *attrs)
{
    struct brokercfg *cfg;

    if (!(cfg = calloc (1, sizeof (*cfg))))
        return NULL;
    cfg->h = h;
    if (!path)
        path = getenv ("FLUX_CONF_DIR");
    if (path) {
        if (!(cfg->path = strdup (path)))
            goto error;
    }
    if (brokercfg_parse (h, path) < 0)
        goto error;
    if (attr_add (attrs, "config.path", path, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        goto error;
    return cfg;
error:
    brokercfg_destroy (cfg);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
