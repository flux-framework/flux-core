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
 *
 * Disconnect handling:  when a client that has one or more requests
 * pending on waitqueues disconnects, you may wish to find its wait_t's
 * and destroy them.  Use wait_set_id() in combination with cmb_msg_sender()
 * to associate a wait_t with the unique sender id, and in the disconnect
 * handler, call wait_destroy_byid ().
 */

typedef struct wait_struct *wait_t;
typedef struct waitqueue_struct *waitqueue_t;

typedef void (*WaitDestroyCb)(wait_t w, void *arg);

/* Create a wait_t.
 * The wait_t takes ownership of zmsg (orig copy will be set to NULL).
 */
wait_t wait_create (flux_t h, int typemask, zmsg_t **zmsg,
                     FluxMsgHandler cb, void *arg);

/* Destroy a wait_t.
 * If zmsg is non-NULL, it is assigned the wait_t's zmsg, if any.
 * Otherwise the zmsg is destroyed.
 */
void wait_destroy (wait_t w, zmsg_t **zmsg);

/* Create/destroy/get length of a waitqueue_t.
 */
waitqueue_t wait_queue_create (void);
void wait_queue_destroy (waitqueue_t q);
int wait_queue_length (waitqueue_t q);

/* Add a wait_t to a queue.
 * You may add a wait_t to multiple queues.
 * Each wait_addqueue increases a wait_t's usecount by one.
 */
void wait_addqueue (waitqueue_t q, wait_t w);

/* Run one wait_t.
 * This decreases the wait_t's usecount by one.  If the usecount reaches zero,
 * the message handler is restarted and the wait_t is destroyed.
 */
void wait_runone (wait_t w);

/* Dequeue all wait_t's from the specified queue.
 * This decreases a wait_t's usecount by one.  If the usecount reaches zero,
 * the message handler is restarted and the wait_t is destroyed.
 * Note: wait_runqueue() empties the waitqueue_t before invoking message
 * handlers, so it is OK to manipulate the waitqueue_t (for example
 * calling wait_addqueue()) from within a handler that was queued on it.
 */
void wait_runqueue (waitqueue_t q);

/* Associated an id string with a wait_t.
 */
void wait_set_id (wait_t w, const char *id);

/* Find all the wait_t's on a queue that match 'id' and destroy them.
 */
void wait_destroy_byid (waitqueue_t q, const char *id);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

