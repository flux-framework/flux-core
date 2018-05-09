#ifndef _BROKER_PUBLISHER_H
#define _BROKER_PUBLISHER_H

typedef int (*publisher_send_f)(void *arg, const flux_msg_t *msg);

struct publisher *publisher_create (void);
void publisher_destroy (struct publisher *pub);

int publisher_set_flux (struct publisher *pub, flux_t *h);

/* Add a sender.  All senders are called when an event is published.
 * If a sender returns -1, an error will be logged but sending will continue.
 * Senders should return 0 on success.
 */
int publisher_set_sender (struct publisher *pub, const char *name,
                          publisher_send_f cb, void *arg);

/* Publish an encoded event message, assigning sequence number.
 */
int publisher_send (struct publisher *pub, const flux_msg_t *msg);

#endif /* !_BROKER_PUBLISHER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
