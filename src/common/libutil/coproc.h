#ifndef _UTIL_COPROC_H
#define _UTIL_COPROC_H

#include <stdbool.h>

typedef struct coproc_struct *coproc_t;
typedef int (*coproc_cb_t)(coproc_t c, void *arg);

/* Destroy a coproc, freeing its stack.
 */
void coproc_destroy (coproc_t c);

/* Create a new coproc, allocating its stack.
 */
coproc_t coproc_create (coproc_cb_t cb);

/* Coproc calls this to yield back to coproc_start/resume caller.
 * Returns 0 on success, -1 on failure with errno set.
 */
int coproc_yield (coproc_t c);

/* Start a coproc.
 * Returns 0 on success, -1 on failure with errno set.
 */
int coproc_start (coproc_t c, void *arg);

/* Resume a coproc.
 * Returns 0 on success, -1 on failure with errno set.
 */
int coproc_resume (coproc_t c);

/* Return true if coproc has returned (as opposed to yielded or not started).
 * If it has returned and 'rc' is non-NULL, set 'rc' to return value.
 */
bool coproc_returned (coproc_t c, int *rc);

/* Return true if coproc has been started with coproc_start(), but
 * has not yet returned.
 */
bool coproc_started (coproc_t c);

#endif /* !_UTIL_COPROC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
