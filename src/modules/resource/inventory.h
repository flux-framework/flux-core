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
 * R is configured resource object, if any (ref taken).
 * R is obtained from enclosing Flux instance or probed dynamically otherwise.
 */
struct inventory *inventory_create (struct resource_ctx *ctx, json_t *R);
void inventory_destroy (struct inventory *inv);

json_t *inventory_get_xml (struct inventory *inv);

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

int inventory_put_xml (struct inventory *inv, json_t *xml);

#endif /* !_FLUX_RESOURCE_INVENTORY_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
