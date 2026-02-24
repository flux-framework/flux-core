/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_METHOD_H
#define _FLUX_CORE_METHOD_H

#include <flux/core.h>

/* Register the following methods for 'service_name':
 *
 * stats-get
 *   Support "flux module stats".
 *   Return flux_t message counters from flux_get_msgcounters(3).
 *   This method is accessible to guest users.
 *
 * stats-clear
 *   Support "flux module stats --clear".
 *   Clear message counters by calling flux_clr_msgcounters(3).
 *   An event message handler for the same topic string is also registered.
 *   To enable "flux-module stats --clear-all", the caller must also subscribe.
 *   to the event message.
 *
 * config-reload
 *   Support configuration update.
 *   Call flux_set_conf_new(3) with an updated config object.
 *
 * config-update
 *   An alias for config-reload.  This variant is called the first time
 *   that the leader config object is updated.  Built-in broker modules
 *   may override this if they need to treat this update specially.
 *
 * debug
 *   Support "flux module debug".
 *   flux_aux_get (h, "flux::debug_flags") returns the (int *) debug mask
 *   or possibly NULL if no flags have been set.
 *
 * rusage
 *   Support "flux module stats --rusage"
 *   Return getrusage(2) results.
 *
 * ping
 *   Support "flux ping".
 *   Note: the handler assumes that "flux::uuid" has been set in the
 *   flux_t aux container.
 *
 * Note: although these methods are somewhat specific to broker modules,
 * the broker itself also registers them under the "broker" service.
 *
 * All methods are only available for the instance owner, except as noted.
 *
 * See also: RFC 5
 */
int flux_register_default_methods (flux_t *h,
                                   const char *service_name,
                                   flux_msg_handler_t **msg_handlers[]);

#endif /* !_FLUX_CORE_METHOD_H */

// vi:ts=4 sw=4 expandtab
