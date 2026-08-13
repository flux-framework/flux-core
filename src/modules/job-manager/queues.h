/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_QUEUES_H
#define _FLUX_JOB_MANAGER_QUEUES_H

#include <stdbool.h>
#include <jansson.h>
#include <flux/core.h>  /* flux_error_t */

#include "src/common/libczmqcontainers/czmq_containers.h"  /* zlistx_t */

struct queues;
struct queue;

/* Collection lifecycle */
struct queues *queues_create (void);
void queues_destroy (struct queues *queues);

/* Configure queues from a JSON `[queues]` table or NULL in `config`.
 *
 * `queues_configure()` diffs provided config against the current table
 * and applies add/remove/update queue primitives. Initial load is the same
 * diff against a (nearly) empty table. Going from a set of configured queues
 * to an empty [queues] table forces configuration back to the anon queue,
 * removing all current named queues.
 *
 * Added named queues are enabled but stopped.
 * Anon queue is always enabled + started.
 */
int queues_configure (struct queues *queues,
                      json_t *config,
                      flux_error_t *error);

/* Set the global [policy] table (may be NULL). Stored by reference (the
 * queues object increfs it) and used as the per-key base beneath each
 * queue's own policy when computing effective policy. Invalidates the
 * list cache.
 */
void queues_set_global_policy (struct queues *queues, json_t *policy);

/* First-class add/remove/update (used by configure)
 *
 * queues_remove() refuses to remove a queue that other queues name as
 * their parent (RFC 33 virtual queues), failing with EBUSY and an
 * error naming the dependent virtual queues; remove or re-parent them
 * first. (queues_configure() is not subject to this: its up-front
 * validation guarantees a surviving virtual queue's parent survives
 * any reload, so a parent and its virtual queues may be removed
 * together.)
 */
struct queue *queues_add (struct queues *queues,
                          const char *name,
                          json_t *config,
                          flux_error_t *error);
int queues_remove (struct queues *queues,
                   const char *name,
                   flux_error_t *error);

/* Refresh config-derived fields (requires, ...) of an existing queue;
 * administrative state (enable/start bits, reasons, sticky) is preserved.
 * Called by configure for queues present in both old and new config.
 *
 * Returns -1 on error with error->text set to a readable error string.
 */
int queues_update (struct queues *queues,
                   struct queue *q,
                   json_t *config,
                   flux_error_t *error);

/* Lookup a named queue. If name == NULL, returns the anon queue
 */
struct queue *queues_lookup (struct queues *queues,
                             const char *name,
                             flux_error_t *error);

/* Return true if there are named queues in the current configuration,
 * false otherwise.
 */
bool queues_have_named (struct queues *queues);

/* Return a snapshot list of current queue names (empty in anon mode),
 * or NULL with errno set on error. Caller must destroy the list. Safe to
 * iterate while queues are mutated or looked up.
 */
zlistx_t *queues_list_names (struct queues *queues);

/* Test whether the queue named 'name' (NULL = anon) is started.
 * Returns false if name does not resolve to a queue.
 */
bool queues_queue_is_started (struct queues *queues, const char *name);

/* Queue change notification: single stream for all mutations.
 *
 * Events: "add", "remove", "update", "enable", "disable", "start", "stop".
 * Fired per affected queue, including during configure and aggregate
 * operations. Never fired during queues_restore(): restore reconstructs
 * state that was already notified when it originally changed, so consumers
 * that persist or forward changes do not see them replayed.
 *
 * The callback keeps the queue table free of side effects: consumers attach
 * behavior (e.g. the job manager cancels a stopped queue's alloc requests)
 * without the table knowing about it. All mutations are reported uniformly
 * so that future consumers (e.g. announcing queue changes to the scheduler,
 * or persisting runtime-created queues) can attach to the same stream.
 */
typedef void (*queues_change_f) (struct queues *queues,
                                 struct queue *q,
                                 const char *event,
                                 void *arg);
void queues_set_notify (struct queues *queues,
                        queues_change_f cb,
                        void *arg);

/* Serialize/de-serialize queue state for checkpoint/restore:
 */
json_t *queues_save (struct queues *queues);
int queues_restore (struct queues *queues, int version, json_t *o);

/* RPC response payload encoders. Pass 'sched_ready=true' if scheduler
 * is loaded: when false, status presents the queue as stopped with a
 * synthesized reason.
 */
json_t *queue_status_encode (struct queue *q, bool sched_ready);
json_t *queues_list_encode (struct queues *queues);

/* Assemble the full job-manager.queue-list RPC response:
 *   {"queues":[names...],
 *    "conf":{"queues":[{"name":s,"requires"?:[...],"parent"?:s}, ...]}}
 * The "queues" name array is retained for backwards compatibility; "conf"
 * carries the effective per-queue configuration (a virtual queue's
 * 'requires' is inherited from its parent). Queue order is unspecified.
 *
 * The response is cached and rebuilt lazily; any queue mutation
 * invalidates the cache. Returns a BORROWED reference owned by the queues
 * object (do not destroy it; incref if it must outlive the next
 * mutation), or NULL with errno set on error.
 */
json_t *queues_list_response (struct queues *queues);

/* Per-queue accessors
 * If q == NULL, assume anonymous queue.
 */
const char *queue_name (struct queue *q);
/* Effective requires: for a virtual queue (RFC 33) this is the parent's
 * requires, since a vqueue has none of its own; for a non-virtual queue
 * it is the queue's own requires.
 */
json_t *queue_requires (struct queue *q);
bool queue_is_enabled (struct queue *q);
const char *queue_disable_reason (struct queue *q);
/* Own started bit; see queue_is_started_effective() below for a
 * virtual queue's effective state.
 */
bool queue_is_started (struct queue *q);
const char *queue_stop_reason (struct queue *q);

/* Virtual queue (RFC 33) accessors.
 *
 * A queue is virtual iff its config sets 'parent'. Inheritance is one
 * level: a vqueue's parent is validated (conf_policy.c and the second
 * validation pass below) to never itself be virtual, so there is no
 * chain to walk - queue_root() is a single pointer dereference.
 */
bool queue_is_virtual (struct queue *q);
/* NULL if not virtual */
struct queue *queue_parent (struct queue *q);
/* 'q's parent if virtual, else 'q' itself */
struct queue *queue_root (struct queue *q);
/* Own started bit AND root's started bit */
bool queue_is_started_effective (struct queue *q);

/* Per-queue mutators
 * These methods fire notification.
 */
int queue_enable (struct queue *q);
int queue_disable (struct queue *q, const char *reason);
int queue_start (struct queue *q, bool nocheckpoint);
int queue_stop (struct queue *q, const char *reason, bool nocheckpoint);

/* Administrative operations by queue name. A NULL name applies the
 * operation to all queues. A reason is required to disable a queue
 * and optional to stop one. Each affected queue fires notify.
 */
int queues_enable_queue (struct queues *queues,
                         const char *name,
                         flux_error_t *error);
int queues_disable_queue (struct queues *queues,
                          const char *name,
                          const char *reason,
                          flux_error_t *error);
int queues_start_queue (struct queues *queues,
                        const char *name,
                        bool nocheckpoint,
                        flux_error_t *error);
int queues_stop_queue (struct queues *queues,
                       const char *name,
                       const char *reason,
                       bool nocheckpoint,
                       flux_error_t *error);

#endif /* ! _FLUX_JOB_MANAGER_QUEUES_H */

// vi:ts=4 sw=4 expandtab
