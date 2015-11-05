#ifndef _FLUX_CORE_WAITQUEUE_H
#define _FLUX_CORE_WAITQUEUE_H

#include <czmq.h>
#include <stdbool.h>
#include <flux/core.h>

/* Waitqueues can be used to stall and restart a message handler.
 * The wait_t contains the message that is being worked on and the
 * message handler callback arguments needed to start the handler over.
 *
 * To stall a message handler, create a wait_t and thread it on one
 * or more waitqueue_t's using wait_addqueue(), then simply abort the
 * handler function.
 *
 * Presumably some other event creates conditions where the handler
 * can be restarted without stalling.
 *
 * When conditions are such that the waiters on a waitqueue_t should
 * try again, run wait_runqueue ().  Once a wait_t is no longer threaded
 * on any waitqueue_t's (its usecount == 0), the handler is restarted.
 */

typedef struct wait_struct wait_t;
typedef struct waitqueue_struct waitqueue_t;

typedef bool (*wait_compare_f)(const flux_msg_t *msg, void *arg);

/* Create/destroy a wait_t.
 */
wait_t *wait_create (flux_t h, flux_msg_handler_t *w, const flux_msg_t *msg,
                     flux_msg_handler_f cb, void *arg);
void wait_destroy (wait_t *wait, double *msec);

/* Create/destroy/get length of a waitqueue_t.
 */
waitqueue_t *wait_queue_create (void);
void wait_queue_destroy (waitqueue_t *q);
int wait_queue_length (waitqueue_t *q);

/* Add a wait_t to a queue.
 * You may add a wait_t to multiple queues.
 * Each wait_addqueue increases a wait_t's usecount by one.
 */
void wait_addqueue (waitqueue_t *q, wait_t *wait);

/* Run one wait_t.
 * This decreases the wait_t's usecount by one.  If the usecount reaches zero,
 * the message handler is restarted and the wait_t is destroyed.
 */
void wait_runone (wait_t *wait);

/* Dequeue all wait_t's from the specified queue.
 * This decreases a wait_t's usecount by one.  If the usecount reaches zero,
 * the message handler is restarted and the wait_t is destroyed.
 * Note: wait_runqueue() empties the waitqueue_t before invoking message
 * handlers, so it is OK to manipulate the waitqueue_t (for example
 * calling wait_addqueue()) from within a handler that was queued on it.
 */
void wait_runqueue (waitqueue_t *q);

/* Destroy all wait_t's on 'q' containing messages that 'cb' returns true on.
 * Return 0 if at least one wait_t is destroyed, or -1 on error.
 */
int wait_destroy_match (waitqueue_t *q, wait_compare_f cb, void *arg);

#endif /* !_FLUX_CORE_WAITQUEUE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

