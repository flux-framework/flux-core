/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
j************************************************************/

#ifndef _LIBSDEXEC_PROPERTY_H
#define _LIBSDEXEC_PROPERTY_H

#include <jansson.h>
#include <flux/core.h>

/* 'Get' method-call.
 * Parse the returned value with sdexec_property_get_unpack().
 */
flux_future_t *sdexec_property_get (flux_t *h,
                                    const char *service,
                                    uint32_t rank,
                                    const char *path,
                                    const char *name);
int sdexec_property_get_unpack (flux_future_t *f, const char *fmt, ...);

/* 'GetAll' method-call.
 * sdexec_property_get_all_dict() accesses the returned property dict,
 * which can be further parsed with sdexec_property_dict_unpack().
 */
flux_future_t *sdexec_property_get_all (flux_t *h,
                                        const char *service,
                                        uint32_t rank,
                                        const char *path);
json_t *sdexec_property_get_all_dict (flux_future_t *f);

/* 'PropertiesChanged' signal.
 * sdexec_property_changed() subscribes to streaming property updates for the
 * specified path.  Each response contains a property dict that may be accessed
 * with sdexec_property_changed_dict().  The dict can be further parsed with
 * sdexec_property_dict_unpack().  Use path=NULL for no path filter, then
 * sdexec_property_changed_path() to get the path for each response.
 */
flux_future_t *sdexec_property_changed (flux_t *h,
                                        const char *service,
                                        uint32_t rank,
                                        const char *path);
json_t *sdexec_property_changed_dict (flux_future_t *f);
const char *sdexec_property_changed_path (flux_future_t *f);

/* Parse property 'name' from property dict.
 */
int sdexec_property_dict_unpack (json_t *dict,
                                 const char *name,
                                 const char *fmt,
                                 ...);

#endif /* !_LIBSDEXEC_PROPERTY_H */

// vi:ts=4 sw=4 expandtab
