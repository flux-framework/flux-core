#ifndef _BROKER_SHUTDOWN_H
#define _BROKER_SHUTDOWN_H

/* Manage the shutdown process for the comms session.
 *
 * Design:
 * The receipt of a "cmb.shutdown" event simultaneously initiates the
 * clean shutdown path in the broker and arms the shutdown timer here.
 * If the clean shutdown path succeedes, it calls shutdown_disarm() to
 * disarm the timer before exiting.  Otherwise, the timer expires, calling
 * the shutdown callback, which calls exit(3).
 *
 * Actual state of affairs:
 * There is no clean shutdown path.  The grace period allows the comms
 * session to propagate the shutdown event, the timer expires, and all
 * ranks exit(3).
 */

typedef struct shutdown_struct shutdown_t;
typedef void (*shutdown_cb_f)(shutdown_t *s, void *arg);

/* Create/destroy shutdown_t.
 */
shutdown_t *shutdown_create (void);
void shutdown_destroy (shutdown_t *s);

/* Set the flux_t handle to be used to configure the grace timer watcher
 * and log the shutdown message.
 */
void shutdown_set_handle (shutdown_t *s, flux_t h);

/* Reigster a shutdown callback to be called when the grace timeout
 * expires.  It is expected to call exit(3).
 */
void shutdown_set_callback (shutdown_t *s, shutdown_cb_f cb, void *arg);

/* Shutdown callback may call this to obtain the broker exit code
 * encoded in the shutdown event.
 */
int shutdown_get_rc (shutdown_t *s);

/* Call shutdown recvmsg() on receipt of "cmb.shutdown" event.
 * This arms the timer.  On rank 0 it also logs the shutdown message e.g.
 * "broker: shutdown in 2s: reason..."
 */
int shutdown_recvmsg (shutdown_t *s, const flux_msg_t *msg);

/* Call shutdown_arm() when shutdown should begin.
 * This sends the "cmb.shutdown" event to all ranks.
 */
int shutdown_arm (shutdown_t *s, double grace, int rc, const char *fmt, ...);

/* Call shutdown_disarm() once the clean shutdown path has succeeded.
 * This disarms the timer on the local rank only.
 */
void shutdown_disarm (shutdown_t *s);

/* Shutdown event encode/decode
 * (used internally, exposed for testing)
 */
flux_msg_t *shutdown_vencode (double grace, int rc, int rank,
                              const char *fmt, va_list ap);
flux_msg_t *shutdown_encode (double grace, int rc, int rank,
                             const char *fmt, ...);
int shutdown_decode (const flux_msg_t *msg, double *grace, int *rc, int *rank,
                     char *reason, int reason_len);


#endif /* !_BROKER_SHUTDOWN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
