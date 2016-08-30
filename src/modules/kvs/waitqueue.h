#ifndef _FLUX_KVS_WAITQUEUE_H
#define _FLUX_KVS_WAITQUEUE_H

#include <stdbool.h>
#include <flux/core.h>

typedef struct wait_struct wait_t;
typedef struct waitqueue_struct waitqueue_t;

typedef void (*wait_cb_f)(void *arg);

/* Create/destroy a wait_t.
 */
wait_t *wait_create (wait_cb_f cb, void *arg);
void wait_destroy (wait_t *wait);

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

/* Dequeue all wait_t's from the specified queue.
 * This decreases a wait_t's usecount by one.  If the usecount reaches zero,
 * the callback is called and the wait_t is destroyed.
 * Note: wait_runqueue() empties the waitqueue_t before invoking callbacks,
 * so it is OK to manipulate the waitqueue_t.
 */
void wait_runqueue (waitqueue_t *q);

/* Specialized wait_t for restarting message handlers (must be idempotent!).
 * The message handler will be reinvoked once the wait_t usecount reaches zero.
 * Message will be copied and destroyed with the wait_t.
 */
wait_t *wait_create_msg_handler (flux_t h, flux_msg_handler_t *w,
                                 const flux_msg_t *msg,
                                 flux_msg_handler_f cb, void *arg);

/* Destroy all wait_t's fitting message match critieria, tested with
 * wait_test_msg_f callback.
 */
typedef bool (*wait_test_msg_f)(const flux_msg_t *msg, void *arg);
int wait_destroy_msg (waitqueue_t *q, wait_test_msg_f cb, void *arg);


#endif /* !_FLUX_KVS_WAITQUEUE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

