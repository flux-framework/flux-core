/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _RESOURCE_QUEUE_MEMBERSHIP_H
#define _RESOURCE_QUEUE_MEMBERSHIP_H

#include <stdbool.h>
#include <jansson.h>
#include <flux/core.h>

/* Apply the membership properties of the statically configured queues to
 * resource object 'R', in place.
 *
 * For each queue in the [queues] config table that declares a 'hosts' key
 * (an RFC 29 hostlist, or the literal "all"), derive the queue's membership
 * property -- the queue name prefixed with the RFC 20 '+' marker ("+NAME") --
 * and apply it to the ranks in R that correspond to those hosts.
 *
 * The job-manager is the authority for queue configuration, so deriving
 * membership here is a deliberate exception, limited to the queues defined
 * in the configuration.
 *
 * It exists because there is no way to synchronize with the scheduler that
 * properties have been established: the acquire stream is one-way, so a
 * scheduler cannot acknowledge that it applied them. A scheduler that has
 * not applied "+NAME" may reject a job that names it as unsatisfiable, and
 * that rejection is permanent. The properties must therefore be sent with
 * the initial acquire response, which only this module can do.
 *
 * The configuration is always present and is re-read on every load, so it
 * can be consulted directly here. Queues created at runtime have no such
 * authority to read, and will need both a push from the job-manager and a
 * way to synchronize with the scheduler.
 *
 * The result is derived state and is not meant to be stored: 'R' here is
 * the copy the module serves to the scheduler and to resource.status, not
 * the one committed to the KVS. Keeping the derivation out of the stored R
 * is what lets it be recomputed from scratch on every load, so a changed,
 * dropped, or deleted queue leaves nothing behind.
 *
 * A queue that does not declare 'hosts' gets no membership property.
 *
 * A 'hosts' value naming an unknown host, or resolving to an empty set of
 * ranks, is a fatal configuration error.
 *
 * Returns 0 on success (including when no queue declares 'hosts'), or -1 on
 * error with 'errp' set.
 */
int queue_membership_apply (json_t *R,
                            const flux_conf_t *conf,
                            flux_error_t *errp);

/* Return a copy of 'R' that queue_membership_apply() may modify without
 * affecting the original. Only execution.properties is duplicated; the
 * rest of R is shared, so the caller must not modify the result's other
 * deep keys. Returns NULL on error with errno set. Caller must
 * json_decref() the result.
 */
json_t *queue_membership_copy (json_t *R);

#endif /* !_RESOURCE_QUEUE_MEMBERSHIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
