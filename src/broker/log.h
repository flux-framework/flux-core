#ifndef BROKER_LOG_H
#define BROKER_LOG_H

#include <flux/core.h>
#include "attr.h"

typedef void (*logbuf_sleeper_f)(const flux_msg_t *msg, void *arg);

typedef struct logbuf_struct logbuf_t;

logbuf_t *logbuf_create (void);
void logbuf_destroy (logbuf_t *logbuf);

void logbuf_set_flux (logbuf_t *logbuf, flux_t h);
void logbuf_set_rank (logbuf_t *logbuf, uint32_t rank);

int logbuf_register_attrs (logbuf_t *logbuf, attr_t *attrs);

/* Clear the buffer of entries <= seq_index
 * Set seq_index = -1 to clear all.
 */
void logbuf_clear (logbuf_t *logbuf, int seq_index);

/* Get next entry with seq > seq_index
 * Set seq_index = -1 to get the first entry.
 * 'seq' is assigned the sequence number of the returned entry.
 */
int logbuf_get (logbuf_t *logbuf, int seq_index, int *seq,
                 const char **buf, int *len);

/* Call 'fun' when anther item is added to the ring buffer.
 * This is how we implement a "nonblocking" dmesg request.
 */
int logbuf_sleepon (logbuf_t *logbuf, logbuf_sleeper_f fun,
                    flux_msg_t *msg, void *arg);

/* Handle disconnects from sleeping dmesg requestors.
 */
void logbuf_disconnect (logbuf_t *logbuf, const char *uuid);

/* Receive a log entry.
 */
int logbuf_append (logbuf_t *logbuf, const char *buf, int len);

/* Receive a log entry.
 * This is a flux_log_f that can be passed to flux_log_redirect()
 * and is used to capture log entries from the broker itself.
 */
void logbuf_append_redirect (const char *buf, int len, void *arg);

#endif /* BROKER_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
