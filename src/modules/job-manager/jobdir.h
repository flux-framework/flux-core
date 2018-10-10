#ifndef _FLUX_JOB_MANAGER_JOBDIR_H
#define _FLUX_JOB_MANAGER_JOBDIR_H

typedef int (*jobdir_map_f)(flux_jobid_t id, int priority,
                            uint32_t userid, void *arg);

/* call 'cb' once for each job found in KVS 'dirname', for jobs stored in
 * FLUID_STRING_DOTHEX (A.B.C.D) form.
 * Returns number of jobs mapped, or -1 on error.
 */
int jobdir_map (flux_t *h, const char *dirname, jobdir_map_f cb, void *arg);

#endif /* !_FLUX_JOB_MANAGER_JOBDIR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

