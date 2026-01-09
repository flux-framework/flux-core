/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* compat.c - temporary help for porting overlay to module environment */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <inttypes.h>
#include <stdio.h>

#include "compat.h"

int compat_attr_add (flux_t *h, const char *name, const char *val, int flags)
{
    if (!val)
        return compat_attr_delete (h, name, true);
    if (flux_attr_set (h, name, val) < 0)
        return -1;
    if ((flags & ATTR_IMMUTABLE)) {
        if (flux_attr_set_cacheonly (h, name, val) < 0)
            return -1;
    }
    return 0;
}

int compat_attr_delete (flux_t *h, const char *name, bool force)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h,
                             "attr.rm",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name", name))
        || flux_rpc_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    (void)flux_attr_set_cacheonly (h, name, NULL);
    flux_future_destroy (f);
    return 0;
}

int compat_attr_add_int (flux_t *h, const char *name, int val, int flags)
{
    char s[32];

    if (snprintf (s, sizeof (s), "%d", val) >= sizeof (s)) {
        errno = EOVERFLOW;
        return -1;
    }
    return compat_attr_add (h, name, s, flags);
}

int compat_attr_add_uint32 (flux_t *h,
                            const char *name,
                            uint32_t val,
                            int flags)
{
    char s[32];

    if (snprintf (s, sizeof (s), "%"PRIu32, val) >= sizeof (s)) {
        errno = EOVERFLOW;
        return -1;
    }
    return compat_attr_add (h, name, s, flags);
}

int compat_attr_get (flux_t *h, const char *name, const char **val, int *flags)
{
    const char *value;
    if (!(value = flux_attr_get (h, name)))
        return -1;
    if (val)
        *val = value;
    if (flags)
        *flags = 0;
    return 0;
}

int compat_attr_set_flags (flux_t *h, const char *name, int flags)
{
    return 0;
}

// vi:ts=4 sw=4 expandtab
