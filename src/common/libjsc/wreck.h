#ifndef _FLUX_CORE_WRECK_H
#define _FLUX_CORE_WRECK_H

/* N.B. these functions internally subscribe to events and register
 * message handlers on the flux_t handle provided to wreck_create(),
 * and require the reactor to run in order to make progress.
 */

struct wreck;

struct wreck_job {
    int64_t id;
    int state;

    /* resources requested */
    int nnodes;
    int ntasks;
    int64_t walltime;

    /* internal jstatctl use only */
    int jsc_state;

    /* internal use only */
    char *kvs_path;
    struct wreck *wreck;
    void *aux;
    flux_free_f aux_free;
    int refcount;
    int fetch_outstanding;
    int fetch_errors;
};

enum {
    WRECK_STATE_RESERVED    = 0x01, // job created and KVS schema updated
    WRECK_STATE_SUBMITTED   = 0x02, // notify scheduler to schedule this job
    WRECK_STATE_STARTING    = 0x04, // first task started
    WRECK_STATE_RUNNING     = 0x08, // startup successful
    WRECK_STATE_FAILED      = 0x10, // error during startup (*)
    WRECK_STATE_COMPLETE    = 0x20, // all tasks exited (*)
    WRECK_STATE_ALL         = 0x3f,
};
// (*) terminal state


/* job->state transitioned to one of the states in 'notify_mask'.
 * If the new state is a terminal one (marked with * above),
 * 'job' will be destroyed after the callback returns.
 */
typedef void (*wreck_notify_f)(struct wreck_job *job, void *arg);

/* Attach 'aux' to job with optional destructor.
 * The destructor is called when the job is destroyed as described above.
 */
void wreck_job_aux_set (struct wreck_job *job, void *aux, flux_free_f destroy);

/* A notify callback may take a reference on a 'job', to delay
 * its destruction by a terminal state transition, if desired.
 */
struct wreck_job *wreck_job_incref (struct wreck_job *job);
void wreck_job_decref (struct wreck_job *job);

/* Register a callback to be invoked upon job state change to one
 * of the states in 'notify_mask'.  Set 'cb' to NULL or
 * notify_mask to zero to unregister.
 *
 * Return 0 on success, -1 on failure with errno set.
 */
int wreck_set_notify (struct wreck *wreck, int notify_mask,
                      wreck_notify_f cb, void *arg);

int wreck_str2state (const char *str);
const char *wreck_state2str (int state);

/* Look up job information by id.
 * Caller must call wreck_job_decref() on result to free.
 */
struct wreck_job *wreck_job_lookup (struct wreck *wreck, int64_t id);

/* Set wreck to a new state.
 * KVS state is updated, then event is issued when that completes.
 * This function on the other hand returns immediately.
 */
int wreck_set_state (struct wreck_job *job, int state);

/* Assign resources (updating KVS, sending event).
 * 'resources' is string-serialized JSON containing an array of
 * {"rank":N,"corecount":M} entries.  Wreck will distribute tasks
 * over this allocation using a hardwired algorithm.
 * N.B. this function returns before the action is complete.
 * Returns 0 on success, -1 on failure with errno set.
 */
int wreck_launch (struct wreck_job *job, const char *resources);

struct wreck *wreck_create (flux_t *h);
void wreck_destroy (struct wreck *wreck);

#endif /* !_FLUX_CORE_WRECK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
