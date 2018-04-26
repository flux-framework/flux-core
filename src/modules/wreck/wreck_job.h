#ifndef HAVE_WJOB_H
#define HAVE_WJOB_H

#include <time.h>
#include <stdint.h>
#include <czmq.h>
#include <flux/core.h>

struct wreck_job {
    int64_t id;
    int refcount;
    char *kvs_path;
    char state[16];
    int nnodes;
    int ntasks;
    int ncores;
    int ngpus;
    int walltime;
    void *aux;
    flux_free_f aux_destroy;
    struct timespec mtime;
};

void wreck_job_destroy (struct wreck_job *job);
struct wreck_job *wreck_job_create (void);
void wreck_job_incref (struct wreck_job *job);

/* Set job status.
 * 'status' must be a string of 15 characters or less.
 * wreck_job_get_state() returns an empty string if job is NULL.
 */
void wreck_job_set_state (struct wreck_job *job, const char *status);
const char *wreck_job_get_state (struct wreck_job *job);

/* Insert job into hash.
 * wreck_job_destroy() will be called upon wreck_job_delete() of this entry,
 * or destruction of the hash.
 * Returns 0 on success, -1 on on failure, with errno set.
 */
int wreck_job_insert (struct wreck_job *job, zhash_t *hash);

/* Remove job from hash, invoking destructor.
 * This is a no-op if 'id' is not found in the hash.
 */
void wreck_job_delete (int64_t id, zhash_t *hash);

/* Look up job in hash by id.
 * Returns job on success, NULL on failure with errno set.
 */
struct wreck_job *wreck_job_lookup (int64_t id, zhash_t *hash);

/* List entries in a hash of jobs, returning a serialized JSON object.
 * The object contains a single array entry under the key "jobs".
 * The array contains entries sorted by reverse "mtime" order of the form:
 *   {"jobid":I "kvs_path":s "state":s}
 * If max > 0, only the first 'max' entries are added to the array.
 * If include_states or exclude_states are non-NULL, they represent a comma-
 * separated list of states to include or exclude from the list, respectively.
 * Return a string-encoded JSON object (caller must free) or NULL on failure
 * with errno set.
 */
char *wreck_job_list (zhash_t *hash, int max, const char *include_states,
                                              const char *exclude_states);

/* Associate data and optional destructor with job.
 */
void wreck_job_set_aux (struct wreck_job *job, void *item, flux_free_f destroy);
void *wreck_job_get_aux (struct wreck_job *job);


#endif /* !HAVE_WJOB_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
