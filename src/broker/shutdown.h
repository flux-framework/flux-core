#ifndef _BROKER_SHUTDOWN_H
#define _BROKER_SHUTDOWN_H

typedef struct shutdown_struct *shutdown_t;

shutdown_t shutdown_create (void);
void shutdown_destroy (shutdown_t s);

void shutdown_set_loop (shutdown_t s, zloop_t *zloop);
void shutdown_set_handle (shutdown_t s, flux_t h);

/* Any rank can call shutdown_arm() to initiate shutdown after 'grace'
 * seconds.  This generates a session-wide event which is then fed into
 * shutdown_recvmsg() on each rank, which starts the grace timeout.
 * The broker should perform graceful teardown then call shutdown_complete()
 * to disarm the timer.  If it fails to do that within the grace period,
 * the timeout handler will cal exit(rc), provided the event loop is
 * still running.
 */
void shutdown_complete (shutdown_t s);
void shutdown_recvmsg (shutdown_t s, zmsg_t *zmsg);
void shutdown_arm (shutdown_t s, int grace, int rc, const char *fmt, ...);

#endif /* !_BROKER_SHUTDOWN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
