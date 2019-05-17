/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONF_H
#    define _FLUX_CORE_CONF_H

#    ifdef __cplusplus
extern "C" {
#    endif

enum {
    CONF_FLAG_INTREE = 1,
};

const char *flux_conf_get (const char *name, int flags);

#    ifdef __cplusplus
}
#    endif

#endif /* !_FLUX_CORE_CONF_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
