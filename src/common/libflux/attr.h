/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_ATTR_H
#define _FLUX_CORE_ATTR_H

/* broker attributes
 *
 * Brokers have configuration attributes.
 * Values are local to a particular broker rank.
 * Some may be overridden on the broker command line with -Sattr=val.
 * The following commands are available for manipulating attributes
 * on the running system:
 *   flux lsattr [-v]
 *   flux setattr name value
 *   flux getattr name
 * In additon, the following functions may be used to get/set broker
 * attributes programmatically.
 */

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Get the value for attribute 'name' from the local broker.
 * Returns value on success, NULL on failure with errno set.
 * This function performs a synchronous RPC to the broker if the
 * attribute is not found in cache, thus may block for the round-trip
 * communication.
 */
const char *flux_attr_get (flux_t *h, const char *name);

/* Set the value for attribute 'name' from the local broker.
 * Returns value on success, NULL on failure with errno set.
 * This function performs a synchronous RPC to the broker,
 * thus blocks for the round-trip communication.
 */
int flux_attr_set (flux_t *h, const char *name, const char *val);


/* hotwire flux_attr_get()'s cache for testing */
int flux_attr_set_cacheonly (flux_t *h, const char *name, const char *val);


/* Get "rank" attribute, and convert to an unsigned integer.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_get_rank (flux_t *h, uint32_t *rank);

/* Get "size" attribute, and convert to an unsigned integer.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_get_size (flux_t *h, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
