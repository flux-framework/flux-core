#ifndef _BROKER_HELLO_H
#define _BROKER_HELLO_H

/* hello protocol is used to detect that TBON overlay has wired up.
 * All the ranks send a small hello request to rank 0.
 * When all the ranks are accounted for, the nodeset is complete.
 * On rank 0, a callback is called when the nodeset is complete.
 * The callback may also be called periodically while the nodeset is
 * coming together, e.g. to report progress or enforce a timeout.
 */

typedef struct hello_struct *hello_t;

typedef void (*hello_cb_f)(hello_t h, void *arg);

hello_t hello_create (void);
void hello_destroy (hello_t h);

void hello_set_overlay (hello_t h, overlay_t ov);
void hello_set_zloop (hello_t h, zloop_t *zloop);

/* Get/set session size
 */
void hello_set_size (hello_t h, uint32_t size);
uint32_t hello_get_size (hello_t h);

/* If defined, callback will be called:
 * - when nodeset is complete
 * - every timeout seconds, until nodeset is complete
 */
void hello_set_cb (hello_t h, hello_cb_f cb, void *arg);

/* Set the timeout period.
 * If this is not called, the callback will only be called
 * when the nodeset is complete.  This function may be called
 * with an argument of zero to disable the timeout.
 */
void hello_set_timeout (hello_t h, double sec);

/* Get time in seconds elapsed since hello_start()
 */
double hello_get_time (hello_t h);

/* Get nodeset string of nodes that have checked in.
 */
const char *hello_get_nodeset (hello_t h);

/* Get count of nodes that have checked in.
 * Nodeset is complete when hello_get_count() == hello_get_size().
 */
uint32_t hello_get_count (hello_t h);

/* Process a received cmb.hello message.
 */
int hello_recv (hello_t h, zmsg_t **zmsg);

/* Start the hello protocol.
 * On rank 0, enable timer (if timeout value has been set)
 * On ranks > 0, send a cmb.hello message to parent.
 */
int hello_start (hello_t h, uint32_t rank);

#endif /* !_BROKER_HELLO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
