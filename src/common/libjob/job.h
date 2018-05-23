#ifndef _FLUX_CORE_JOB_H
#define _FLUX_CORE_JOB_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t flux_jobid_t;

flux_future_t *flux_job_add (flux_t *h, const char *J, int flags);
int flux_job_add_get_id (flux_future_t *f, flux_jobid_t *id);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
