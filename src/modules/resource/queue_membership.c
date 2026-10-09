/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queue_membership.c - apply static queue membership properties to R
 *
 * A configured queue that declares 'hosts' (RFC 33) has its membership
 * property "+NAME" (an RFC 20 instance-local property) applied to the
 * corresponding ranks of the resource object R at module load time. See
 * queue_membership.h for why these are derived here rather than obtained
 * from the job-manager, the authority for queue configuration.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/librlist/rlist.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libfluxutil/conf_policy.h"
#include "ccan/str/str.h"

#include "queue_membership.h"

/* Resolve a queue's 'hosts' value to an idset string of R ranks. 'hosts' is
 * either the literal "all" (every rank in 'rl') or an RFC 29 hostlist. An
 * unknown host or an empty result is a fatal error. Caller frees the result.
 */
static char *hosts_to_idset_string (struct rlist *rl,
                                    const char *name,
                                    const char *hosts,
                                    flux_error_t *errp)
{
    struct idset *ranks;
    char *s = NULL;

    if (streq (hosts, "all"))
        ranks = rlist_ranks (rl);
    else
        ranks = rlist_hosts_to_ranks (rl, hosts, errp);
    if (!ranks) {
        /* rlist_hosts_to_ranks() already set errp for the hostlist case. */
        if (streq (hosts, "all"))
            errprintf (errp, "queue %s: no resources configured", name);
        return NULL;
    }
    if (idset_count (ranks) == 0) {
        errprintf (errp, "queue %s: hosts '%s' is an empty set", name, hosts);
        errno = EINVAL;
        goto done;
    }
    if (!(s = idset_encode (ranks, IDSET_FLAG_RANGE)))
        errprintf (errp, "queue %s: idset_encode: %s", name, strerror (errno));
done:
    idset_destroy (ranks);
    return s;
}

/* True if any queue in 'queues' declares 'hosts', i.e. there is a membership
 * property to derive.
 */
static bool queue_membership_configured (json_t *queues)
{
    const char *name;
    json_t *entry;

    json_object_foreach (queues, name, entry) {
        json_t *hosts = json_object_get (entry, "hosts");
        if (hosts && json_is_string (hosts))
            return true;
    }
    return false;
}

/* Return a copy of 'R' that shares everything except execution.properties,
 * which is duplicated so queue_membership_apply() can modify it without
 * touching the original.
 *
 * Only the objects along the path to that key are copied, so R's bulk --
 * notably 'scheduling', which carries the JGF on a system instance -- is
 * shared rather than duplicated. The caller must therefore not modify the
 * result's other deep keys.
 */
json_t *queue_membership_copy (json_t *R)
{
    json_t *cpy = NULL;
    json_t *execution = NULL;
    json_t *props = NULL;
    json_t *orig_props;

    if (!(cpy = json_copy (R))
        || !(execution = json_copy (json_object_get (R, "execution"))))
        goto nomem;
    /* execution.properties is absent until a membership property is
     * applied, in which case apply() creates it in the copy.
     */
    if ((orig_props = json_object_get (execution, "properties"))) {
        if (!(props = json_copy (orig_props))
            || json_object_set (execution, "properties", props) < 0)
            goto nomem;
    }
    if (json_object_set (cpy, "execution", execution) < 0)
        goto nomem;
    json_decref (execution);
    json_decref (props);
    return cpy;
nomem:
    json_decref (cpy);
    json_decref (execution);
    json_decref (props);
    errno = ENOMEM;
    return NULL;
}

int queue_membership_apply (json_t *R,
                            const flux_conf_t *conf,
                            flux_error_t *errp)
{
    json_t *queues = NULL;
    const char *name;
    json_t *entry;
    struct rlist *rl = NULL;
    json_t *newR = NULL;
    json_t *newprop;
    int rc = -1;

    if (flux_conf_unpack (conf, NULL, "{s?o}", "queues", &queues) < 0)
        queues = NULL;
    /* Nothing to derive without a 'hosts' queue, which is the common case.
     * Check before building the rlist below, which is O(ranks).
     */
    if (!queue_membership_configured (queues))
        return 0;

    /* Build an rlist from R so hosts can be resolved to ranks and the derived
     * membership properties applied, then encoded back into R.
     */
    if (!(rl = rlist_from_json (R, NULL))) {
        errprintf (errp, "queue membership: error parsing R");
        errno = EINVAL;
        goto done;
    }
    json_object_foreach (queues, name, entry) {
        const char *hosts;
        char *ranks;
        char prop[QUEUE_PROPERTY_MAX];

        /* Only queues that declare 'hosts' get a membership property. A
         * virtual queue must not set 'hosts' (enforced by conf_policy), so a
         * 'hosts' queue always uses its own name for the property.
         */
        if (json_unpack (entry, "{s:s}", "hosts", &hosts) < 0)
            continue;
        if (!(ranks = hosts_to_idset_string (rl, name, hosts, errp)))
            goto done;
        (void)snprintf (prop, sizeof (prop), "+%s", name);
        if (rlist_add_property (rl, errp, prop, ranks) < 0) {
            free (ranks);
            goto done;
        }
        free (ranks);
    }
    /* Splice the updated properties from the decorated rlist back into R.
     * rlist_to_R() emits the full R; take only execution.properties so R's
     * other fields (scheduling, etc.) are preserved.
     */
    if (!(newR = rlist_to_R (rl))) {
        errprintf (errp, "queue membership: rlist_to_R: %s", strerror (errno));
        goto done;
    }
    /* At least one property was applied above, so the key is present. */
    if (!(newprop = jpath_get (newR, "execution.properties"))
        || jpath_set (R, "execution.properties", newprop) < 0) {
        errprintf (errp, "queue membership: error updating R properties");
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    json_decref (newR);
    rlist_destroy (rl);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
