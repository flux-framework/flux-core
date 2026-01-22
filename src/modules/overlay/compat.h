/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _OVERLAY_COMPAT_H
#define _OVERLAY_COMPAT_H

enum {
    ATTR_IMMUTABLE = 1,
};

int compat_attr_add (flux_t *h, const char *name, const char *val, int flags);
int compat_attr_delete (flux_t *h, const char *name, bool force);
int compat_attr_add_int (flux_t *h, const char *name, int val, int flags);
int compat_attr_add_uint32 (flux_t *h,
                            const char *name,
                            uint32_t val,
                            int flags);
int compat_attr_get (flux_t *h, const char *name, const char **val, int *flags);
int compat_attr_set_flags (flux_t *h, const char *name, int flags);

#endif /* !_OVERLAY_COMPAT_H */


// vi:ts=4 sw=4 expandtab
