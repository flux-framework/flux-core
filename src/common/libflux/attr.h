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
 * In addition, the following functions may be used to get/set broker
 * attributes programmatically.
 */

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

/* Iterate over the attribute names that are stored in the local
 * attribute cache.
 */
const char *flux_attr_cache_first (flux_t *h);
const char *flux_attr_cache_next (flux_t *h);

/* Get "rank" attribute, and convert to an unsigned integer.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_get_rank (flux_t *h, uint32_t *rank);

/* Get "size" attribute, and convert to an unsigned integer.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_get_size (flux_t *h, uint32_t *size);

/* Look up hostname of broker rank, by consulting "hostlist" attribute.
 * This function always returns a printable string, though it may be "(null)".
 */
const char *flux_get_hostbyrank (flux_t *h, uint32_t rank);

/* Find the lowest numbered broker rank running on 'host', by consulting
 * the "hostlist" attribute.
 * Returns rank on success, -1 on failure with errno set.
 */
int flux_get_rankbyhost (flux_t *h, const char *host);

/* Return a list/set of hosts/ranks in Hostlist/Idset form given 'targets'
 * in Idset/Hostlist form. Caller must free returned value.
 *
 * Returns NULL on failure with error message in errp->text (if errp != NULL).
 *
 * NOTES:
 *  - The source of the mapping is the rank-ordered broker 'hostlist' attribute.
 *  - An Idset (RFC 22) is a set (unordered, no duplicates)
 *  - A Hostlist (RFC 29) is a list (ordered, may be duplicates)
 *  - If there are multiple ranks per host, this function can only map
 *    hostnames to the first rank found on the host.
 *
 */
char *flux_hostmap_lookup (flux_t *h,
                           const char *targets,
                           flux_error_t *errp);

/* Look up the broker.starttime attribute on rank 0.
 * The instance uptime is flux_reactor_now() - starttime.
 * N.B. if the instance has been restarted, this value is the most
 * recent restart time.
 */
int flux_get_instance_starttime (flux_t *h, double *starttime);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_ATTR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
