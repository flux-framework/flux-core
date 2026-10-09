/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBFLUXUTIL_CONF_POLICY_H
#define _LIBFLUXUTIL_CONF_POLICY_H

#include <flux/core.h>

/* Size of the buffer the membership property "+NAME" of a queue that
 * declares 'hosts' is derived into, and the resulting maximum length of
 * such a queue's name (leaving room for the '+' and the terminating NUL).
 * conf_policy_validate() enforces the name length so the derivation cannot
 * truncate, which would otherwise merge two queues into one property.
 */
#define QUEUE_PROPERTY_MAX 128
#define QUEUE_NAME_MAX (QUEUE_PROPERTY_MAX - 2)

/* Validate [policy] and [queue] configuration tables defined in RFC 33.
 * Return 0 on success, -1 on failure with errno set and 'error' filled in.
 */
int conf_policy_validate (const flux_conf_t *conf, flux_error_t *error);

#endif // !_LIBFLUXUTIL_CONF_POLICY_H

// vi:ts=4 sw=4 expandtab
