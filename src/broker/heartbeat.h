#ifndef _BROKER_HEARTBEAT_H
#define _BROKER_HEARTBEAT_H

typedef struct {
    zloop_t *zloop;
    double rate;
    int timer_id;
    int epoch;
    zloop_timer_fn *cb;
    void *cb_arg;
} heartbeat_t;

heartbeat_t *heartbeat_create (void);
void heartbeat_destroy (heartbeat_t *hb);

/* Default rate (seconds) can be overridden with set function.
 * Will return -1, errno == EINVAL if out of hardwired range.
 * set_ratestr parses a double with optional time suffix ('s' or 'ms').
 */
int heartbeat_set_rate (heartbeat_t *hb, double rate);
int heartbeat_set_ratestr (heartbeat_t *hb, const char *s);
double heartbeat_get_rate (heartbeat_t *hb);

/* Get/set the current epoch.
 */
void heartbeat_set_epoch (heartbeat_t *hb, int epoch);
int heartbeat_get_epoch (heartbeat_t *hb);
void heartbeat_next_epoch (heartbeat_t *hb);

/* The generator of heartbeats (rank 0) installs a timer and in
 * the handler, calls next_epoch() and event_encode().
 */
void heartbeat_set_zloop (heartbeat_t *hb, zloop_t *zloop);
void heartbeat_set_cb (heartbeat_t *hb, zloop_timer_fn *cb, void *arg);
int heartbeat_start (heartbeat_t *hb);
void heartbeat_stop (heartbeat_t *hb);
zmsg_t *heartbeat_event_encode (heartbeat_t *hb);

/* A passive receiver of heartbeats (rank > 0) processes a
 * received heartbeat message with event_decode(), setting the epoch.
 */
int heartbeat_event_decode (heartbeat_t *hb, zmsg_t *zmsg);

#endif /* !_BROKER_HEARTBEAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
