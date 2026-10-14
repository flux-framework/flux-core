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
 * in the configuration. It closes a startup race: the job-manager loads
 * after this module, and the scheduler acquires R from it, so there is no
 * one to ask when R is first built. The configuration is always present
 * and re-read on every load, so it can be consulted directly, and the
 * result is correct across a reload of this module, of the scheduler, or
 * of the whole instance.
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

/* True if any queue in 'queues' (the [queues] config table, or NULL)
 * declares 'hosts', i.e. there is a membership property to derive.
 */
bool queue_membership_configured (json_t *queues);

#endif /* !_RESOURCE_QUEUE_MEMBERSHIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
