/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_HOSTS_H
#define _FLUX_CORE_HOSTS_H

#ifdef __cplusplus
extern "C" {
#endif

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
char *flux_hostmap_lookup (flux_t *h, const char *targets, flux_error_t *errp);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_HOSTS_H */

// vi:ts=4 sw=4 expandtab
