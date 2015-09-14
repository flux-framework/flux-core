#ifndef BROKER_LOG_H
#define BROKER_LOG_H

#include <flux/core.h>

typedef void (*log_sleeper_f)(const flux_msg_t *msg, void *arg);

typedef struct log_struct log_t;

log_t *log_create (void);
void log_destroy (log_t *log);

void log_set_flux (log_t *log, flux_t h);
void log_set_rank (log_t *log, uint32_t rank);
void log_set_file (log_t *log, FILE *f);

int log_set_level (log_t *log, int level);
int log_get_level (log_t *log);

int log_set_buflimit (log_t *log, int limit);
int log_get_buflimit (log_t *log);

int log_get_bufcount (log_t *log);
int log_get_count (log_t *log);

/* Clear the buffer of entries <= seq_index
 * Set seq_index = -1 to clear all.
 */
void log_buf_clear (log_t *log, int seq_index);

/* Get next entry in json log format with seq > seq_index
 * Set seq_index = -1 to get the first entry.
 * 'seq' is assigned the sequence number of the returned entry.
 */
const char *log_buf_get (log_t *log, int seq_index, int *seq);

/* Call 'fun' when anther item is added to the ring buffer.
 * This is how we implement a "nonblocking" dmesg request.
 */
int log_buf_sleepon (log_t *log, log_sleeper_f fun, flux_msg_t *msg, void *arg);

/* Handle disconnects from sleeping dmesg requestors.
 */
void log_buf_disconnect (log_t *log, const char *uuid);

/* Receive a log entry in json log format.
 */
int log_append_json (log_t *log, const char *json_str);

/* Receive a fully decoded log entry.
 * This is a flux_log_f that can be passed to flux_log_redirect()
 * and is used to capture log entries from the broker itself.
 */
void log_append_redirect (const char *facility, int level, uint32_t rank,
                          struct timeval tv, const char *s, void *arg);

#endif /* BROKER_LOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
