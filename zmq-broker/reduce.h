typedef struct red_struct *red_t;

enum {
    FLUX_RED_TIMEDFLUSH = 1,// flush timer starts on first append when empty

    FLUX_RED_HWMFLUSH = 2,  // initially every append is flushed; after
                            //   batchnum is incremented, hwm is calculated
                            //   from previous batch and next flush is at hwm
};

typedef void   (*FluxRedFn)(flux_t h, zlist_t *items, int batchnum, void *arg);
typedef void   (*FluxSinkFn)(flux_t h, void *item, int batchnum, void *arg);

/* Create/destroy a reduction handle.
 * The sink function will be called every time the handle is flushed.
 * Flush occurs according to reduction flags (see defs above).
 * If no flags, sink is only called manually via flux_red_flush().
 */
red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, void *arg);
void flux_red_destroy (red_t r);

/* Set the reduction function (optional).
 * Reduction function will be called every time an item is appended.
 */
void flux_red_set_reduce_fn (red_t r, FluxRedFn redfn);

/* Set reduction flags.
 */
void flux_red_set_flags (red_t r, int flags);

/* Set the timeout value used with FLUX_RED_TIMEDFLUSH
 */
void flux_red_set_timeout_msec (red_t r, int msec);

/* Map the sink function over each item in the handle.
 */
void flux_red_flush (red_t r);

/* Append an item to the reduction handle.
 * The reduction function is immediately called (if defined).
 * The sink function is called according to flags.
 * Returns 0 on success, -1 on failure.
 */
int flux_red_append (red_t r, void *item, int batchnum);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
