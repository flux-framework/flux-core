/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_MODHASH_H
#define _BROKER_MODHASH_H

#include <jansson.h>

#include "src/common/librouter/disconnect.h"

#include "attr.h"
#include "service.h"
#include "module.h"

typedef struct modhash modhash_t;

/* Hash-o-modules, keyed by uuid
 * Destructor returns the number of modules that had to be canceled.
 */
modhash_t *modhash_create (void);
int modhash_destroy (modhash_t *mh);

void modhash_add (modhash_t *mh, module_t *p);
void modhash_remove (modhash_t *mh, module_t *p);

/* Send an event message to all modules that have matching subscription.
 */
int modhash_event_mcast (modhash_t *mh, const flux_msg_t *msg);

/* Send a response message to the module whose uuid matches the
 * next hop in the routing stack.
 */
int modhash_response_sendmsg (modhash_t *mh, const flux_msg_t *msg);

/* Find a module matching 'uuid'.
 */
module_t *modhash_lookup (modhash_t *mh, const char *uuid);

/* Find a module matching 'name'.
 * Either the module name or the path given to module_add() works.
 * N.B. this is a slow linear search - keep out of crit paths
 */
module_t *modhash_lookup_byname (modhash_t *mh, const char *name);

/* Prepare RFC 5 'mods' array for lsmod response.
 */
json_t *modhash_get_modlist (modhash_t *mh,
                             double now,
                             struct service_switch *sw);

/* Iterator
 */
module_t *modhash_first (modhash_t *mh);
module_t *modhash_next (modhash_t *mh);

#endif /* !_BROKER_MODHASH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
