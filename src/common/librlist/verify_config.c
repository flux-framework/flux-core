/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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

#include <errno.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/ptrint/ptrint.h"

#include "verify_config.h"

struct rlist_verify_config {
    enum rlist_verify_mode default_mode;
    zhashx_t *overrides;  // resource -> verify_mode (via int2ptr)
    bool explicit_config; // true if a config object explicitly provided
};

static int parse_verify_mode (const char *mode_str,
                              enum rlist_verify_mode *mode)
{
    if (streq (mode_str, "strict"))
        *mode = RLIST_VERIFY_STRICT;
    else if (streq (mode_str, "allow-extra"))
        *mode = RLIST_VERIFY_ALLOW_EXTRA;
    else if (streq (mode_str, "allow-missing"))
        *mode = RLIST_VERIFY_ALLOW_MISSING;
    else if (streq (mode_str, "ignore"))
        *mode = RLIST_VERIFY_IGNORE;
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static bool is_valid_resource_type (const char *name)
{
    if (name == NULL)
        return false;
    return streq (name, "default")
        || streq (name, "hostname")
        || streq (name, "core")
        || streq (name, "gpu");
}

int rlist_verify_config_update (struct rlist_verify_config *config,
                                json_t *verify_obj,
                                flux_error_t *errp)
{
    const char *key;
    json_t *value;

    if (!verify_obj)
        return 0;

    if (!json_is_object (verify_obj)
        && !json_is_boolean (verify_obj)) {
        errno = EINVAL;
        errprintf (errp, "verify must be a table or boolean");
        return -1;
    }

    config->explicit_config = true;

    if (json_is_true (verify_obj)) {
        config->default_mode = RLIST_VERIFY_STRICT;
        zhashx_purge (config->overrides);
        return 0;
    }

    if (json_is_false (verify_obj)) {
        config->default_mode = RLIST_VERIFY_IGNORE;
        zhashx_purge (config->overrides);
        return 0;
    }

    json_object_foreach (verify_obj, key, value) {
        const char *mode_str = json_string_value (value);
        enum rlist_verify_mode mode;

        if (!mode_str) {
            errno = EINVAL;
            errprintf (errp, "verify.%s mode must be a string", key);
            return -1;
        }

        if (parse_verify_mode (mode_str, &mode) < 0) {
            errprintf (errp,
                       "verify.%s: unknown verify mode '%s'",
                       key,
                       mode_str);
            return -1;
        }

        if (streq (key, "default")) {
            config->default_mode = mode;
        } else {
            /* For now we restrict resource types to 'hostname', 'core', or
             * 'gpu', since these are the only types supported by librlist.
             * In the future, arbitrary types could be allowed in the config
             * and this check can be dropped. (more likely librlist would be
             * replaced, though)
             */
            if (!is_valid_resource_type (key)) {
                errprintf (errp, "verify: unsupported resource type: %s", key);
                errno = EINVAL;
                return -1;
            }
            zhashx_update (config->overrides, key, int2ptr (mode));
        }
    }
    return 0;
}

struct rlist_verify_config *rlist_verify_config_create (json_t *verify_obj,
                                                        flux_error_t *errp)
{
    struct rlist_verify_config *config = NULL;

    if (!(config = calloc (1, sizeof (*config)))
        || !(config->overrides = zhashx_new ())) {
        errno = ENOMEM;
        errprintf (errp, "out of memory");
        goto error;
    }

    config->default_mode = RLIST_VERIFY_STRICT;

    if (rlist_verify_config_update (config, verify_obj, errp) < 0)
        goto error;

    return config;
error:
    rlist_verify_config_destroy (config);
    return NULL;
}

void rlist_verify_config_destroy (struct rlist_verify_config *config)
{
    if (config) {
        int saved_errno = errno;
        zhashx_destroy (&config->overrides);
        free (config);
        errno = saved_errno;
    }
}

enum rlist_verify_mode
rlist_verify_config_get_mode (const struct rlist_verify_config *config,
                              const char *resource_type)
{
    void *val;

    if (!config)
        return RLIST_VERIFY_STRICT;

    val = zhashx_lookup (config->overrides, resource_type);
    if (val)
        return (enum rlist_verify_mode) ptr2int (val);
    return config->default_mode;
}

int rlist_verify_config_set_mode (struct rlist_verify_config *config,
                                  const char *type,
                                  enum rlist_verify_mode mode)
{
    if (!config || !is_valid_resource_type (type)) {
        errno = EINVAL;
        return -1;
    }
    if (streq (type, "default"))
        config->default_mode = mode;
    else
        zhashx_update (config->overrides, type, int2ptr (mode));
    return 0;
}

bool rlist_verify_config_is_explicit (const struct rlist_verify_config *config)
{
    return config && config->explicit_config;
}

/* vi: ts=4 sw=4 expandtab
 */
