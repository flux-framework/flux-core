#ifndef _BROKER_HELLO_H
#define _BROKER_HELLO_H

#include <stdbool.h>

/* hello protocol is used to detect that TBON overlay has wired up.
 * All the ranks send a small hello request to rank 0.
 * When all the ranks are accounted for, the nodeset is complete.
 * On rank 0, a callback is called when the nodeset is complete.
 * The callback may also be called periodically while the nodeset is
 * coming together, e.g. to report progress or enforce a timeout.
 */

typedef struct hello_struct hello_t;

typedef void (*hello_cb_f)(hello_t *hello, void *arg);

hello_t *hello_create (void);
void hello_destroy (hello_t *hello);

/* The flux handle is used
 * - to obtain flux_rank() and flux_size()
 * - to send messages to rank 0
 * - to set up a recurring reactor timer to call hello_cb_f if any
 */
void hello_set_flux (hello_t *hello, flux_t h);

/* If defined, callback will be called:
 * - when nodeset is complete
 * - every timeout seconds, until nodeset is complete
 */
void hello_set_callback (hello_t *hello, hello_cb_f cb, void *arg);

/* Set the timeout period.
 * If this is not called, the callback will only be called
 * when the nodeset is complete.  This function may be called
 * with an argument of zero to disable the timeout.
 */
void hello_set_timeout (hello_t *hello, double sec);

/* Get time in seconds elapsed since hello_start()
 */
double hello_get_time (hello_t *hello);

/* Get nodeset string of nodes that have checked in.
 */
const char *hello_get_nodeset (hello_t *hello);

/* Get completion status
 */
bool hello_complete (hello_t *hello);

/* Process a received cmb.hello message.
 */
int hello_recvmsg (hello_t *hello, const flux_msg_t *msg);

/* Start the hello protocol.
 * On rank 0, enable timer (if timeout value has been set)
 * On ranks > 0, send a cmb.hello message to parent.
 */
int hello_start (hello_t *hello);

/* Hello request encode/decode.
 * Used internally but exposed for testing.
 */
flux_msg_t *hello_encode (int rank);
int hello_decode (const flux_msg_t *msg, int *rank);

#endif /* !_BROKER_HELLO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
