#ifndef _FLUX_JOB_MANAGER_ACTIVE_H
#define _FLUX_JOB_MANAGER_ACTIVE_H

/* Operations on active jobs in KVS
 */

#include <flux/core.h>
#include "job.h"

/* Write KVS path to 'key' relative to active job directory for 'job'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int active_key (char *buf, int bufsz, struct job *job, const char *key);

/* Set 'key' within active job directory for 'job'.
 */
int active_pack (flux_kvs_txn_t *txn,
                 struct job *job,
                 const char *key,
                 const char *fmt, ...);

/* Log an event to eventlog 'key', relative to active job directory for 'job'.
 * The event consists of current wallclock, 'name', and optional context
 * formatted from (fmt, ...).  Set fmt="" to skip logging a context.
 */
int active_eventlog_append (flux_kvs_txn_t *txn,
                            struct job *job,
                            const char *key,
                            const char *name,
                            const char *fmt, ...);

/* Unlink the active job directory for 'job'.
 */
int active_unlink (flux_kvs_txn_t *txn, struct job *job);


/* active_map callback should return -1 on error to stop map with error,
 * or 0 on success.  'job' is only valid for the duration of the callback.
 */
typedef int (*active_map_f)(struct job *job, void *arg);

/* call 'cb' once for each job found in active job directory.
 * Returns number of jobs mapped, or -1 on error.
 */
int active_map (flux_t *h, active_map_f cb, void *arg);


#endif /* _FLUX_JOB_MANAGER_ACTIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

