/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_INVENTORY_H
#define _FLUX_RESOURCE_INVENTORY_H

/* Create resource inventory.
 * config->R is configured resource object, if any (ref taken).
 * R is obtained from enclosing Flux instance or probed dynamically otherwise.
 */
struct inventory *inventory_create (struct resource_ctx *ctx,
                                    struct resource_config *config);
void inventory_destroy (struct inventory *inv);

/* Get resource object.
 * Returned resource object shall not be modified or freed by the caller.
 */
json_t *inventory_get (struct inventory *inv);

/* Get the method used to construct the resource object.
 * (NULL if resource object is unavailable, errno set)
 */
const char *inventory_get_method (struct inventory *inv);

/* Set resource object from internal discovery.
 * Takes a reference on 'R' but does not copy.
 * Caller shall not modify R after calling this function.
 * This triggers writing of resource.R to the KVS, and posting of
 * 'resource-define' to resource.eventlog.  The KVS commits are asynchronous.
 */
int inventory_put (struct inventory *inv, json_t *R, const char *method);

/* Return a set of ranks for a string of "targets". The 'targets' argument
 * may be an RFC22 encoded idset or RFC29 hostlist. If an idset, the
 * decoded idset is returned, if a hostlist, then the set of ranks
 * corresponding to the hostnames in 'targets' is returned.
 *
 * On error, a textual error string will be returned in errbuf
 */
struct idset *inventory_targets_to_ranks (struct inventory *inv,
                                          const char *targets,
                                          flux_error_t *errp);

/* Get the number of execution targets in R
 */
int inventory_get_size (struct inventory *inv);

#endif /* !_FLUX_RESOURCE_INVENTORY_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
