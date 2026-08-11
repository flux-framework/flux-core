/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queues.c - queues configuration and state for job-manager
 *
 * This module owns:
 *   struct queues  - a collection of configured RFC 33 queues
 *   struct queue   - a single queue's state
 *
 * The service layer (queue.c) delegates all queue state here and drives
 * side effects (enqueue/dequeue, log) from the change-notification callback.
 *
 * See also:
 * RFC 33/Flux Job Queues
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "queues.h"

struct queue {
    char *name;                 /* NULL for the anonymous queue */
    bool is_enabled;            /* jobs may be submitted to this queue */
    char *disable_reason;       /* reason if disabled */
    bool is_started;            /* current queue state */
    bool is_started_sticky;     /* tracks is_started unless --nocheckpoint */
    char *stop_reason;          /* reason if stopped (optionally set) */
    json_t *requires;           /* required properties array (own; a
                                  * virtual queue's is always NULL - see
                                  * queue_root() for the effective value)
                                  */
    struct queue *parent;       /* resolved parent queue (RFC 33 virtual
                                  * queues), or NULL if not virtual. Not
                                  * owned; borrowed from the same queues
                                  * table. Inheritance is one level
                                  * (validated elsewhere) so this is
                                  * never itself virtual.
                                  */
    struct queues *queues;      /* back-pointer for notify */
};

struct queues {
    struct queue *anon;         /* live when named == NULL */
    zhashx_t *named;            /* non-NULL selects named mode */
    bool restoring;             /* suppress notify during queues_restore */
    queues_change_f notify_cb;
    void *notify_arg;
};

/* Internal: fire change notification if callback is registered.
 * Suppressed during queues_restore: restore reconstructs state that
 * was already notified when it originally changed.
 */
static void notify (struct queues *queues,
                    struct queue *q,
                    const char *event)
{
    if (queues->notify_cb && !queues->restoring)
        queues->notify_cb (queues, q, event, queues->notify_arg);
}

static void queue_free (struct queue *q)
{
    if (q) {
        int saved_errno = errno;
        json_decref (q->requires);
        free (q->name);
        free (q->disable_reason);
        free (q->stop_reason);
        free (q);
        errno = saved_errno;
    }
}

/* zhashx_destructor_fn signature */
static void queue_destructor (void **item)
{
    if (item) {
        queue_free (*item);
        *item = NULL;
    }
}

/* Extract the "parent" key from a queue's own config entry 'entry' (may
 * be NULL). On success, '*namep' is set to the borrowed name string,
 * or NULL if 'entry' has no "parent" key. Returns -1 if "parent" is
 * present but not a string, filling in 'jerror' (which may be NULL) with
 * jansson's diagnostic ("expected string").
 */
static int entry_parent_name (json_t *entry,
                              const char **namep,
                              json_error_t *jerror)
{
    *namep = NULL;
    if (!entry)
        return 0;
    return json_unpack_ex (entry, jerror, 0, "{s?s}", "parent", namep);
}

/* True if 'name's config entry in 'table' (the full desired [queues]
 * config, or NULL) declares a "parent", i.e. 'name' is to be virtual.
 * Used by queues_config_validate() to check a candidate parent's own
 * virtual-ness directly against the desired config JSON, since at
 * validation time the corresponding queue objects may not have their
 * ->parent pointers resolved yet (JSON object iteration order is
 * arbitrary).
 */
static bool entry_is_virtual (json_t *table, const char *name)
{
    const char *parent_name;

    return table
        && entry_parent_name (json_object_get (table, name),
                              &parent_name,
                              NULL) == 0
        && parent_name != NULL;
}

/* Wire virtual queue 'q's ->parent pointer from its config 'entry'
 * (RFC 33 virtual queues). Called only from queues_configure()'s second
 * pass, after queues_config_validate() has already checked every parent
 * reference in the same config (string, not self, resolves, not itself
 * virtual). Those checks are thus guaranteed to hold and are not
 * repeated here.
 *
 * Returns -1 on failure with errno set to EPROTO. This indicates a
 * previous failure to validate, or some other unexpected error.
 */
static int resolve_parent (struct queue *q, json_t *entry)
{
    struct queue *parent;
    struct queues *queues = q->queues;
    const char *parent_name;

    q->parent = NULL;
    if (entry_parent_name (entry, &parent_name, NULL) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (!parent_name)
        return 0;
    if (!queues->named
        || !(parent = zhashx_lookup (queues->named, parent_name))) {
        errno = EPROTO;
        return -1;
    }
    q->parent = parent;
    return 0;
}

/* Lookup and validate the "parent" key of config 'entry' (with queue 'name')
 * against the live config table, without mutating any state. This is useful
 * when validating a parent entry on a first config pass, as required by
 * queues_add()/queues_updated(). 'name' is the queue being added/updated
 * (NULL for the anon queue).
 *
 * On success returns 0 and sets '*parentp' to the parent queue, or NULL if
 * 'entry' declares no parent. Returns -1 with 'error' filled in if 'parent'
 * is malformed, not configured, names 'name' itself, is itself virtual, or
 * is declared on the anonymous queue (whose creation removes every named
 * queue, including any would-be parent).
 *
 * Note: Typically libfluxutil/conf_policy.c duplicates this same validation,
 * so the checks done here are defense-in-depth.
 */
static int lookup_parent (struct queues *queues,
                          const char *name,
                          json_t *entry,
                          struct queue **parentp,
                          flux_error_t *error)
{
    struct queue *parent;
    const char *parent_name;
    json_error_t jerror;

    *parentp = NULL;
    if (entry_parent_name (entry, &parent_name, &jerror) < 0) {
        errprintf (error,
                  "queue '%s': %s",
                  name ? name : "(anon)",
                  jerror.text);
        errno = EINVAL;
        return -1;
    }
    if (!parent_name)
        return 0;
    if (!name) {
        errprintf (error, "the anonymous queue cannot have a parent");
        errno = EINVAL;
        return -1;
    }
    if (streq (parent_name, name)) {
        errprintf (error, "queue '%s': parent queue is itself", name);
        errno = EINVAL;
        return -1;
    }
    if (!queues->named
        || !(parent = zhashx_lookup (queues->named, parent_name))) {
        errprintf (error,
                  "queue '%s': parent queue '%s' is not configured",
                  name,
                  parent_name);
        errno = EINVAL;
        return -1;
    }
    if (queue_is_virtual (parent)) {
        errprintf (error,
                  "queue '%s': parent queue '%s' is itself a virtual queue",
                  name,
                  parent_name);
        errno = EINVAL;
        return -1;
    }
    *parentp = parent;
    return 0;
}

/* Parse the 'requires' config-derived field shared by queue_alloc() and
 * queues_update(). 'parent' (RFC 33 virtual queues) is handled
 * separately by resolve_parent() above, straight from the config JSON.
 */
static int queue_parse_config (struct queue *q, json_t *config)
{
    json_t *requires = NULL;

    if (config && json_unpack (config, "{s?O}", "requires", &requires) < 0) {
        errno = EINVAL;
        return -1;
    }
    json_decref (q->requires);
    q->requires = requires;   /* may be NULL */
    return 0;
}

static struct queue *queue_alloc (struct queues *queues,
                                  const char *name,
                                  json_t *config)
{
    struct queue *q;

    if (!(q = calloc (1, sizeof (*q))))
        return NULL;
    q->queues = queues;
    if (name && !(q->name = strdup (name)))
        goto error;
    q->is_enabled = true;

    if (queue_parse_config (q, config) < 0)
        goto error;

    /* The anonymous queue begins life started; named queues do not. */
    if (name)
        q->is_started_sticky = q->is_started = false;
    else
        q->is_started_sticky = q->is_started = true;
    return q;
error:
    queue_free (q);
    return NULL;
}

/* Collection lifecycle */

struct queues *queues_create (void)
{
    struct queues *queues;

    if (!(queues = calloc (1, sizeof (*queues))))
        return NULL;
    /* Start with anonymous queue: enabled + started */
    if (!(queues->anon = queue_alloc (queues, NULL, NULL))) {
        free (queues);
        return NULL;
    }
    return queues;
}

void queues_destroy (struct queues *queues)
{
    if (queues) {
        int saved_errno = errno;
        if (queues->named)
            zhashx_destroy (&queues->named);
        else
            queue_free (queues->anon);
        free (queues);
        errno = saved_errno;
    }
}

/* Change notification */

void queues_set_notify (struct queues *queues,
                        queues_change_f cb,
                        void *arg)
{
    queues->notify_cb = cb;
    queues->notify_arg = arg;
}

/* Lookup */

struct queue *queues_lookup (struct queues *queues,
                             const char *name,
                             flux_error_t *error)
{
    if (name) {
        struct queue *q;

        if (!queues->named
            || !(q = zhashx_lookup (queues->named, name))) {
            errprintf (error, "'%s' is not a valid queue", name);
            return NULL;
        }
        return q;
    }

    /* Otherwise, return anon queue
     */
    if (queues->named) {
        errprintf (error, "a named queue is required");
        return NULL;
    }
    return queues->anon;
}

/* Cursor-based iteration over all queues without allocating.
 *
 * N.B. zhashx_lookup repositions the same internal cursor, so this
 * must not be used across operations that fire change notifications
 * or otherwise reenter this class - use queues_list_names for that.
 */
static struct queue *queues_first (struct queues *queues)
{
    if (queues->named)
        return zhashx_first (queues->named);
    return queues->anon;
}

static struct queue *queues_next (struct queues *queues)
{
    if (queues->named)
        return zhashx_next (queues->named);
    return NULL;
}

bool queues_have_named (struct queues *queues)
{
    return queues->named != NULL;
}

zlistx_t *queues_list_names (struct queues *queues)
{
    zlistx_t *names;

    if (queues->named)
        names = zhashx_keys (queues->named);
    else
        names = zlistx_new ();
    if (!names) {
        errno = ENOMEM;
        return NULL;
    }
    return names;
}

bool queues_queue_is_started (struct queues *queues, const char *name)
{
    struct queue *q;

    if (queues->named) {
        if (!name || !(q = zhashx_lookup (queues->named, name)))
            return false;
    }
    else {
        if (name)
            return false;
        q = queues->anon;
    }
    /* RFC 33 virtual queues: a job is eligible for scheduling only
     * when both its own queue and (if virtual) the parent queue are
     * started.  For a non-virtual queue, queue_root (q) == q.
     */
    return q->is_started && queue_root (q)->is_started;
}

/* First-class add/remove/update primitives */

/* Remove all named queues, sending a "remove" notification for each.
 * Establishes the anonymous queue and destroys the queues->named hash,
 * which switches the collection back to anonymous mode.
 *
 * Returns -1 on failure, in which case no change has been made (named
 * queues remain intact)
 */
static int remove_all_named_queues (struct queues *queues)
{
    zlistx_t *names;
    const char *qname;
    struct queue *q;

    /* Establish the anon queue first. If this or creating the queues names
     * list fails, return immediately so named queues remain intact.
     */
    if (!(queues->anon = queue_alloc (queues, NULL, NULL))
        || !(names = zhashx_keys (queues->named))) {
        queue_free (queues->anon);
        queues->anon = NULL;
        errno = ENOMEM;
        return -1;
    }
    qname = zlistx_first (names);
    while (qname) {
        if ((q = zhashx_lookup (queues->named, qname)))
            notify (queues, q, "remove");
        qname = zlistx_next (names);
    }
    zlistx_destroy (&names);
    zhashx_destroy (&queues->named);
    return 0;
}

/* Internal: add a queue to the table without resolving its 'parent'
 * (RFC 33 virtual queues) to a pointer. Used by
 * queues_configure()'s per-entry loop, which defers resolution to a
 * second pass across the *whole* table once every add/update/remove for
 * this configure call has been applied - JSON object iteration order is
 * arbitrary, so a virtual queue's parent may be processed after it, and
 * a reload may re-point an existing queue's already-resolved parent.
 * See queues_configure().
 */
static struct queue *queue_add_internal (struct queues *queues,
                                         const char *name,
                                         json_t *config,
                                         flux_error_t *error)
{
    struct queue *q;
    zhashx_t *named = NULL;

    if (!name) {
        if (!queues->named) {
            /* Already anon - just return the existing anon queue:
             */
            return queues->anon;
        }
        /* Otherwise, adding the anonymous queue means switching to anon
         * mode, since named queues and the anonymous queue cannot coexist.
         * Any existing named queues are removed, with a "remove" notification
         * fired for each, and a fresh anonymous queue (enabled + started) is
         * created. This is how queues_configure() implements a reload that
         * deletes the last [queues] table entry.
         */
        if (remove_all_named_queues (queues) < 0) {
            errprintf (error, "%s", strerror (errno));
            return NULL;
        }
        notify (queues, queues->anon, "add");
        return queues->anon;
    }

    /* Otherwise, a new named queue is being added.
     * First, check for a duplicate name:
     */
    if (queues->named && zhashx_lookup (queues->named, name)) {
        errprintf (error, "queue '%s' already exists", name);
        errno = EEXIST;
        return NULL;
    }

    /* Build the new queue, and when transitioning from anon to named
     * mode the new hash, before mutating any collection state. The anon
     * queue is not torn down until the new queue is successfully in place,
     * so an OOM failure here leaves the prior state (and its observers)
     * intact.
     */
    if (!(q = queue_alloc (queues, name, config)))
        goto error;
    if (!queues->named) {
        if (!(named = zhashx_new ())
            || zhashx_insert (named, name, q) < 0) {
            errno = ENOMEM;
            goto error;
        }
        zhashx_set_destructor (named, queue_destructor);
        notify (queues, queues->anon, "remove");
        queue_free (queues->anon);
        queues->anon = NULL;
        queues->named = named;
    }
    else if (zhashx_insert (queues->named, name, q) < 0) {
        errno = ENOMEM;
        goto error;
    }
    notify (queues, q, "add");
    return q;
error:
    errprintf (error, "%s", strerror (errno));
    ERRNO_SAFE_WRAP (zhashx_destroy, &named);
    queue_free (q);
    return NULL;
}

struct queue *queues_add (struct queues *queues,
                          const char *name,
                          json_t *config,
                          flux_error_t *error)
{
    struct queue *q;
    struct queue *parent;

    /* Unlike queues_configure() (which defers to a whole-table second
     * pass - see queue_add_internal() above), a standalone add has no
     * further "table is now fully populated" step coming, so validate
     * and look up 'parent' now, BEFORE the add mutates anything - see
     * lookup_parent() for what a post-add failure would leave behind.
     */
    if (lookup_parent (queues, name, config, &parent, error) < 0)
        return NULL;
    if (!(q = queue_add_internal (queues, name, config, error)))
        return NULL;
    q->parent = parent;
    return q;
}

/* Internal: remove a queue without checking for dependent virtual
 * queues. Used by queues_configure(), whose up-front validation
 * (queues_config_validate()) already guarantees that any surviving
 * virtual queue's parent also survives, and whose second pass
 * re-resolves every surviving queue's ->parent pointer - so removing
 * a parent mid-configure is safe there and must not be rejected
 * (e.g. a reload that drops a parent and its vqueues together).
 */
static int queue_remove_internal (struct queues *queues, const char *name)
{
    struct queue *q;

    if (!queues->named || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(q = zhashx_lookup (queues->named, name))) {
        errno = ENOENT;
        return -1;
    }
    notify (queues, q, "remove");
    zhashx_delete (queues->named, name);
    return 0;
}

int queues_remove (struct queues *queues,
                   const char *name,
                   flux_error_t *error)
{
    struct queue *q;

    if (!queues->named || !name) {
        errprintf (error, "no named queues or no queue name given");
        errno = EINVAL;
        return -1;
    }
    if (!(q = zhashx_lookup (queues->named, name))) {
        errprintf (error, "queue '%s' does not exist", name);
        errno = ENOENT;
        return -1;
    }
    /* RFC 33 virtual queues: refuse to remove a queue that other
     * queues name as their parent, else their ->parent pointers would
     * dangle (use-after-free on the next status or effective-started
     * query). Callers must remove or re-parent the virtual queues
     * first. queues_configure() is exempt via queue_remove_internal()
     * above - its validation and second pass maintain the invariant
     * across the whole reconfigure.
     */
    if (!queue_is_virtual (q)) {
        char vqueues[128];
        size_t n = 0;
        struct queue *child = queues_first (queues);
        while (child) {
            if (child->parent == q && n < sizeof (vqueues)) {
                int len = snprintf (vqueues + n,
                                    sizeof (vqueues) - n,
                                    "%s%s",
                                    n ? "," : "",
                                    child->name);
                if (len > 0)
                    n += len;
            }
            child = queues_next (queues);
        }
        if (n > 0) {
            errprintf (error,
                       "queue '%s' is the parent of virtual queue%s %s",
                       name,
                       strchr (vqueues, ',') ? "s" : "",
                       vqueues);
            errno = EBUSY;
            return -1;
        }
    }
    return queue_remove_internal (queues, name);
}

/* Internal: refresh config-derived fields of an existing queue without
 * resolving 'parent' to a pointer - see queue_add_internal() above for
 * why queues_configure()'s per-entry loop needs this variant.
 */
static int queue_update_internal (struct queues *queues,
                                  struct queue *q,
                                  json_t *config,
                                  flux_error_t *error)
{
    if (queue_parse_config (q, config) < 0) {
        errprintf (error, "invalid queue config");
        return -1;
    }
    notify (queues, q, "update");
    return 0;
}

int queues_update (struct queues *queues,
                   struct queue *q,
                   json_t *config,
                   flux_error_t *error)
{
    struct queue *parent;

    /* An update may re-parent (or un-parent/parent) an existing queue.
     * As with queues_add(), a standalone update has no whole-table
     * second pass coming, so validate 'parent' BEFORE mutating anything
     * - see lookup_parent() for what a post-mutation failure would
     * leave behind - then apply it along with the config-derived
     * fields, so the "update" observer sees the fully-updated queue
     * (queues_configure() instead notifies per entry and resolves
     * parents in its second pass). N.B. queue_parse_config() mutates
     * nothing when it fails.
     */
    if (lookup_parent (queues, q->name, config, &parent, error) < 0)
        return -1;
    if (queue_parse_config (q, config) < 0) {
        errprintf (error, "invalid queue config");
        return -1;
    }
    q->parent = parent;
    notify (queues, q, "update");
    return 0;
}

/* Configuration */

/* Validate a [queues] config object without mutating any state, so
 * queues_configure() can reject a bad config before its destructive remove
 * phase and thereby keep the reconfigure transactional. Two passes:
 *
 *  1. Structural: each queue's value must be a table.
 *  2. Parent references (RFC 33 virtual queues): a virtual queue's 'parent'
 *     must be a string, must not name the queue itself, must resolve to
 *     another entry in this same config, and that entry must not itself be
 *     virtual (inheritance is one level). These mirror resolve_parent()'s
 *     rules, but are checked here against the desired config JSON up front -
 *     resolve_parent() otherwise runs only in queues_configure()'s second
 *     pass, after queues have already been removed/added, so a bad parent
 *     reaching it there would both partially apply the config and, if the
 *     failing entry's dropped parent had left a sibling virtual queue's
 *     ->parent dangling, leave that stale pointer unresolved. Pre-validating
 *     makes the destructive phase unreachable on any such input.
 *
 * This is deliberately not a full RFC 33 schema check (known keys, 'requires'
 * property array, 'policy' sub-table) - that is done up front by
 * conf_policy_validate() before any config reaches this class. The parent
 * checks here overlap conf_policy_validate()'s, but this class must remain
 * self-protecting against bad config reaching queues_configure() directly.
 */
static int queues_config_validate (json_t *config, flux_error_t *error)
{
    const char *name;
    json_t *value;

    json_object_foreach (config, name, value) {
        if (!json_is_object (value)) {
            errprintf (error, "queue %s: configuration must be a table", name);
            errno = EINVAL;
            return -1;
        }
    }
    json_object_foreach (config, name, value) {
        const char *parent_name;
        json_error_t jerror;

        if (entry_parent_name (value, &parent_name, &jerror) < 0) {
            errprintf (error, "queue '%s': %s", name, jerror.text);
            errno = EINVAL;
            return -1;
        }
        if (!parent_name)
            continue;
        if (streq (parent_name, name)) {
            errprintf (error, "queue '%s': parent queue is itself", name);
            errno = EINVAL;
            return -1;
        }
        if (!json_object_get (config, parent_name)) {
            errprintf (error,
                       "queue '%s': parent queue '%s' is not configured",
                       name,
                       parent_name);
            errno = EINVAL;
            return -1;
        }
        if (entry_is_virtual (config, parent_name)) {
            errprintf (error,
                       "queue '%s': parent queue '%s' is itself a virtual"
                       " queue",
                       name,
                       parent_name);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int queues_configure (struct queues *queues,
                      json_t *config,
                      flux_error_t *error)
{
    if (config && json_object_size (config) > 0) {
        const char *name;
        json_t *value;

        /* Validate the entire config before mutating any state - both its
         * structure and its virtual-queue parent references. The apply phase
         * below removes queues before adding/updating, so rejecting a bad
         * config up front is what keeps a failed reconfigure from leaving a
         * partially-applied configuration (or a dangling ->parent pointer)
         * behind.
         */
        if (queues_config_validate (config, error) < 0)
            return -1;

        /* Remove queues that disappeared from config */
        if (queues->named) {
            zlistx_t *names;
            if (!(names = queues_list_names (queues))) {
                errprintf (error, "out of memory");
                return -1;
            }
            name = zlistx_first (names);
            while (name) {
                if (!json_object_get (config, name)) {
                    if (queue_remove_internal (queues, name) < 0) {
                        errprintf (error,
                                   "remove: %s: %s",
                                   name,
                                   strerror (errno));
                        zlistx_destroy (&names);
                        return -1;
                    }
                }
                name = zlistx_next (names);
            }
            zlistx_destroy (&names);
        }

        /* Add new queues / update existing ones. Parent resolution
         * (RFC 33 virtual queues) is deferred to a second pass below,
         * once the table reflects the full desired config: JSON object
         * iteration order is arbitrary, so a virtual queue's parent
         * entry may not have been added/updated yet when the virtual
         * queue itself is processed, and a reload may change which
         * queue is whose parent.
         */
        json_object_foreach (config, name, value) {
            struct queue *q;
            if (!queues->named
                || !(q = zhashx_lookup (queues->named, name))) {
                if (!queue_add_internal (queues, name, value, error))
                    return -1;
            }
            else {
                /* Existing queue: refresh config-derived state only.
                 * Administrative state (enable/start bits, reasons,
                 * sticky) is preserved per b362f73d7.
                 */
                if (queue_update_internal (queues, q, value, error) < 0)
                    return -1;
            }
        }

        /* Second pass: now that every queue in the desired config is
         * present with its config-derived fields refreshed, resolve
         * 'parent' pointers. queues_config_validate() has already
         * checked every parent reference against this same 'config'
         * above, so resolve_parent() cannot fail here on referential
         * grounds; it re-checks only enough to avoid storing a bogus
         * pointer, and a -1 return would indicate an internal
         * inconsistency (see resolve_parent()), which we still propagate
         * with a caller-facing message.
         */
        json_object_foreach (config, name, value) {
            struct queue *q;
            if (queues->named
                && (q = zhashx_lookup (queues->named, name))
                && resolve_parent (q, value) < 0) {
                errprintf (error,
                           "queue '%s': failed to resolve parent: %s",
                           name,
                           strerror (errno));
                return -1;
            }
        }
    }
    else {
        /* No named queues configured: transition to anon mode
         * (a no-op if already anon)
         */
        if (queues->named) {
            if (!queues_add (queues, NULL, NULL, error))
                return -1;
        }
    }
    return 0;
}

/* Checkpoint */

static int set_string (json_t *o, const char *key, const char *val)
{
    json_t *s = json_string (val);
    if (!s || json_object_set_new (o, key, s) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* "start" is the effective started state. "blocked" is a keyword (RFC 33)
 * identifying why a started queue is not scheduling: "scheduler" if the
 * scheduler is offline, or "parent" for a virtual queue whose parent is
 * stopped. It is included only in that case, alongside a "stop_reason"
 * describing the condition for humans. A consumer can derive the queue's
 * own started bit as (start || blocked-present).
 */
json_t *queue_status_encode (struct queue *q, bool sched_ready)
{
    json_t *o = NULL;
    bool start;
    const char *blocked_reason = NULL;
    const char *stop_reason;
    char *parent_reason = NULL;

    if (!sched_ready) {
        start = false;
        if (q->is_started)
            blocked_reason = "scheduler";
        stop_reason = "Scheduler is offline";
    }
    else {
        start = queue_is_started_effective (q);
        stop_reason = q->stop_reason;
        if (!start && q->is_started) {
            /* Virtual queue with own bit started but parent stopped */
            blocked_reason = "parent";
            if (asprintf (&parent_reason,
                          "parent queue '%s' is stopped",
                          queue_name (queue_parent (q))) < 0) {
                parent_reason = NULL;
                goto error;
            }
            stop_reason = parent_reason;
        }
    }
    if (!(o = json_pack ("{s:b s:b}",
                         "enable", q->is_enabled,
                         "start", start)))
        goto error;
    if (queue_is_virtual (q)) {
        if (set_string (o, "parent", queue_name (queue_parent (q))) < 0)
            goto error;
    }
    if (blocked_reason) {
        if (set_string (o, "blocked", blocked_reason) < 0)
            goto error;
    }
    if (!q->is_enabled && q->disable_reason) {
        if (set_string (o, "disable_reason", q->disable_reason) < 0)
            goto error;
    }
    if (!start && stop_reason) {
        if (set_string (o, "stop_reason", stop_reason) < 0)
            goto error;
    }
    free (parent_reason);
    return o;
error:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (free, parent_reason);
    return NULL;
}

json_t *queues_list_encode (struct queues *queues)
{
    json_t *a;

    if (!(a = json_array ()))
        goto error;
    if (queues->named) {
        struct queue *q = zhashx_first (queues->named);
        while (q) {
            json_t *o;
            if (!(o = json_string (q->name))
                || json_array_append_new (a, o) < 0) {
                /* jansson decrefs the new object on failure */
                goto error;
            }
            q = zhashx_next (queues->named);
        }
    }
    return a;
error:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

static int save_one (json_t *a, struct queue *q)
{
    json_t *entry;

    if (!(entry = json_pack ("{s:b s:b}",
                             "enable", q->is_enabled,
                             "start", q->is_started_sticky)))
        goto error;
    if (q->name) {
        if (set_string (entry, "name", q->name) < 0)
            goto error;
    }
    if (!q->is_enabled) {
        if (set_string (entry, "disable_reason", q->disable_reason) < 0)
            goto error;
    }
    if (!q->is_started_sticky && q->stop_reason) {
        if (set_string (entry, "stop_reason", q->stop_reason) < 0)
            goto error;
    }
    if (json_array_append_new (a, entry) < 0) {
        /* jansson decrefs entry on failure */
        errno = ENOMEM;
        return -1;
    }
    return 0;
error:
    errno = ENOMEM;
    ERRNO_SAFE_WRAP (json_decref, entry);
    return -1;
}

json_t *queues_save (struct queues *queues)
{
    json_t *a;
    struct queue *q;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }
    q = queues_first (queues);
    while (q) {
        if (save_one (a, q) < 0)
            goto error;
        q = queues_next (queues);
    }
    return a;
error:
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

static int restore_v0 (struct queues *queues, json_t *entry)
{
    const char *name = NULL;
    const char *reason = NULL;
    const char *disable_reason = NULL;
    int enable;
    struct queue *q = NULL;

    if (json_unpack (entry,
                     "{s?s s:b s?s s?s}",
                     "name", &name,
                     "enable", &enable,
                     "reason", &reason,
                     "disable_reason", &disable_reason) < 0) {
        errno = EINVAL;
        return -1;
    }

    /* "reason" is backwards compatible field name for "disable_reason" */
    if (!disable_reason && reason)
        disable_reason = reason;

    if ((q = queues_lookup (queues, name, NULL))) {
        if (enable) {
            if (queue_enable (q) < 0)
                return -1;
        }
        else {
            if (queue_disable (q, disable_reason) < 0)
                return -1;
        }
    }
    return 0;
}

static int restore_v1 (struct queues *queues, json_t *entry)
{
    const char *name = NULL;
    const char *disable_reason = NULL;
    const char *stop_reason = NULL;
    int enable;
    int start;
    struct queue *q = NULL;

    if (json_unpack (entry,
                     "{s?s s:b s?s s:b s?s}",
                     "name", &name,
                     "enable", &enable,
                     "disable_reason", &disable_reason,
                     "start", &start,
                     "stop_reason", &stop_reason) < 0) {
        errno = EINVAL;
        return -1;
    }
    /* Find the queue, silently ignoring unknown queue names */
    if ((q = queues_lookup (queues, name, NULL))) {
        if (enable) {
            if (queue_enable (q) < 0)
                return -1;
        }
        else {
            if (queue_disable (q, disable_reason) < 0)
                return -1;
        }
        if (start) {
            if (queue_start (q, false) < 0)
                return -1;
        }
        else {
            if (queue_stop (q, stop_reason, false) < 0)
                return -1;
        }
    }
    return 0;
}

int queues_restore (struct queues *queues, int version, json_t *o)
{
    size_t index;
    json_t *entry;
    int rc = -1;

    if ((version != 0 && version != 1)
        || !o
        || !json_is_array (o)) {
        errno = EINVAL;
        return -1;
    }
    queues->restoring = true;
    json_array_foreach (o, index, entry) {
        if (version == 0) {
            if (restore_v0 (queues, entry) < 0)
                goto done;
        }
        else {
            if (restore_v1 (queues, entry) < 0)
                goto done;
        }
    }
    rc = 0;
done:
    queues->restoring = false;
    return rc;
}

/* Per-queue accessors */

const char *queue_name (struct queue *q)
{
    return q->name;
}

json_t *queue_requires (struct queue *q)
{
    /* Effective requires: a virtual queue (RFC 33) has no requires of
     * its own, so return the parent's. queue_root (q) == q for a
     * non-virtual queue, so this is the queue's own requires there.
     */
    return queue_root (q)->requires;
}

bool queue_is_enabled (struct queue *q)
{
    return q->is_enabled;
}

const char *queue_disable_reason (struct queue *q)
{
    return q->disable_reason;
}

bool queue_is_started (struct queue *q)
{
    return q->is_started;
}

const char *queue_stop_reason (struct queue *q)
{
    return q->stop_reason;
}

/* Virtual queue (RFC 33) accessors */

bool queue_is_virtual (struct queue *q)
{
    return q->parent != NULL;
}

struct queue *queue_parent (struct queue *q)
{
    return q->parent;
}

struct queue *queue_root (struct queue *q)
{
    return q->parent ? q->parent : q;
}

bool queue_is_started_effective (struct queue *q)
{
    return q->is_started && queue_root (q)->is_started;
}

/* Per-queue mutators (fire notify) */

int queue_enable (struct queue *q)
{
    q->is_enabled = true;
    free (q->disable_reason);
    q->disable_reason = NULL;
    notify (q->queues, q, "enable");
    return 0;
}

int queue_disable (struct queue *q, const char *reason)
{
    char *cpy = NULL;
    if (reason && !(cpy = strdup (reason)))
        return -1;
    free (q->disable_reason);
    q->disable_reason = cpy;
    q->is_enabled = false;
    notify (q->queues, q, "disable");
    return 0;
}

int queue_start (struct queue *q, bool nocheckpoint)
{
    q->is_started = true;
    if (!nocheckpoint)
        q->is_started_sticky = q->is_started;
    free (q->stop_reason);
    q->stop_reason = NULL;
    notify (q->queues, q, "start");
    return 0;
}

int queue_stop (struct queue *q, const char *reason, bool nocheckpoint)
{
    char *cpy = NULL;
    if (reason) {
        if (!(cpy = strdup (reason)))
            return -1;
    }
    free (q->stop_reason);
    q->stop_reason = cpy;
    q->is_started = false;
    if (!nocheckpoint)
        q->is_started_sticky = q->is_started;
    notify (q->queues, q, "stop");
    return 0;
}

/* Administrative operations by queue name */

static int foreach_named_queue (struct queues *queues,
                                int (*cb) (struct queue *, void *),
                                void *arg)
{
    int rc = -1;
    struct queue *q;
    zlistx_t *values;

    /* Callback may trigger notify, which may do a queue lookup or other
     * operation that perturbs zhashx cursor, so do not iterate zhashx
     * directly here. Use a values list instead:
     */
    if (!(values = zhashx_values (queues->named))) {
        errno = ENOMEM;
        return -1;
    }
    zlistx_set_destructor (values, NULL);
    q = zlistx_first (values);
    while (q) {
        if ((*cb) (q, arg) < 0)
            goto out;
        q = zlistx_next (values);
    }
    rc = 0;
out:
    ERRNO_SAFE_WRAP (zlistx_destroy, &values);
    return rc;
}

static int enable_cb (struct queue *q, void *arg)
{
    return queue_enable (q);
}

int queues_enable_queue (struct queues *queues,
                         const char *name,
                         flux_error_t *error)
{
    struct queue *q;

    if (!name && queues->named) {
        if (foreach_named_queue (queues, &enable_cb, NULL) < 0)
            return errprintf (error,
                              "failed to enable queues: %s",
                              strerror (errno));
        return 0;
    }
    if (!(q = queues_lookup (queues, name, error)))
        return -1;
    if (queue_enable (q) < 0)
        return errprintf (error,
                          "failed to enable queue: %s",
                          strerror (errno));
    return 0;
}

static int disable_cb (struct queue *q, void *arg)
{
    const char *reason = arg;
    return queue_disable (q, reason);
}

int queues_disable_queue (struct queues *queues,
                          const char *name,
                          const char *reason,
                          flux_error_t *error)
{
    struct queue *q;

    if (!reason) {
        errprintf (error, "reason is required for disable");
        errno = EINVAL;
        return -1;
    }
    if (!name && queues->named) {
        if (foreach_named_queue (queues, &disable_cb, (void *) reason) < 0)
            return errprintf (error,
                              "failed to disable queues: %s",
                              strerror (errno));
        return 0;
    }
    if (!(q = queues_lookup (queues, name, error)))
        return -1;
    if (queue_disable (q, reason) < 0)
        return errprintf (error,
                          "failed to disable queue: %s",
                          strerror (errno));
    return 0;
}

static int start_cb (struct queue *q, void *arg)
{
    bool nocheckpoint = *((bool *)arg);
    return queue_start (q, nocheckpoint);
}

int queues_start_queue (struct queues *queues,
                        const char *name,
                        bool nocheckpoint,
                        flux_error_t *error)
{
    struct queue *q;

    if (!name && queues->named) {
        if (foreach_named_queue (queues, &start_cb, &nocheckpoint) < 0)
            return errprintf (error,
                              "failed to start queues: %s",
                              strerror (errno));
        return 0;
    }
    if (!(q = queues_lookup (queues, name, error)))
        return -1;
    return queue_start (q, nocheckpoint);
}

struct stop_arg {
    bool nocheckpoint;
    const char *reason;
};

static int stop_cb (struct queue *q, void *arg)
{
    struct stop_arg *s_arg = arg;
    return queue_stop (q, s_arg->reason, s_arg->nocheckpoint);
}

int queues_stop_queue (struct queues *queues,
                       const char *name,
                       const char *reason,
                       bool nocheckpoint,
                       flux_error_t *error)
{
    struct queue *q;

    if (!name && queues->named) {
        struct stop_arg arg = {
            .reason = reason,
            .nocheckpoint = nocheckpoint
        };
        if (foreach_named_queue (queues, &stop_cb, &arg) < 0)
            return errprintf (error,
                              "failed to stop queues: %s",
                              strerror (errno));
        return 0;
    }
    if (!(q = queues_lookup (queues, name, error)))
        return -1;
    if (queue_stop (q, reason, nocheckpoint) < 0)
        return errprintf (error,
                          "failed to stop queue: %s",
                          strerror (errno));
    return 0;
}

// vi:ts=4 sw=4 expandtab
