#ifndef _BROKER_HEARTBEAT_H
#define _BROKER_HEARTBEAT_H

typedef struct heartbeat_struct *heartbeat_t;
typedef void (*heartbeat_cb_f)(heartbeat_t h, void *arg);

heartbeat_t heartbeat_create (void);
void heartbeat_destroy (heartbeat_t h);

/* Default rate (seconds) can be overridden with set function.
 * Will return -1, errno == EINVAL if out of hardwired range.
 * set_ratestr parses a double with optional time suffix ('s' or 'ms').
 */
int heartbeat_set_rate (heartbeat_t h, double rate);
int heartbeat_set_ratestr (heartbeat_t h, const char *s);
double heartbeat_get_rate (heartbeat_t h);

/* Get/set the current epoch.
 */
void heartbeat_set_epoch (heartbeat_t h, int epoch);
int heartbeat_get_epoch (heartbeat_t h);
void heartbeat_next_epoch (heartbeat_t h);

/* The generator of heartbeats (rank 0) installs a timer and in
 * the handler, calls next_epoch() and event_encode().
 */
void heartbeat_set_loop (heartbeat_t h, zloop_t *zloop);
void heartbeat_set_cb (heartbeat_t h, heartbeat_cb_f cb, void *arg);
int heartbeat_start (heartbeat_t h);
void heartbeat_stop (heartbeat_t h);
zmsg_t *heartbeat_event_encode (heartbeat_t h);

/* A passive receiver of heartbeats (rank > 0) processes a
 * received heartbeat message with event_decode(), setting the epoch.
 */
int heartbeat_event_decode (heartbeat_t h, zmsg_t *zmsg);

#endif /* !_BROKER_HEARTBEAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
