typedef struct red_struct *red_t;

enum {
    FLUX_RED_AUTOFLUSH = 1, /* immediately sink after append */
    FLUX_RED_TIMEDFLUSH = 2,/* sink after first item aged past timeout */
    FLUX_RED_ADAPTFLUSH = 4,/* timeout followed by hwm-triggered flush */
};

/* Reduction function will be called every time an item is appended
 * to the handle.  It can be NULL to disable reduction.
 */
typedef void   (*FluxRedFn)(flux_t h, zlist_t *items, void *arg);

/* Sink function will be called every time the handle is flushed.
 */
typedef void   (*FluxSinkFn)(flux_t h, void *item, void *arg);

/* Create/destroy a reduction handle.
 */
red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, FluxRedFn redfn,
                       int flags, void *arg);
void flux_red_destroy (red_t r);

/* Map the sink function over each item in the handle.
 */
void flux_red_flush (red_t r);

/* Append an item to the reduction handle.
 * The reduction function is immediately called.
 * If FLUX_RED_AUTOFLUSH is set, the sink function is then called.
 * Returns the number of items queued in the handle (after reduction).
 */
int flux_red_append (red_t r, void *item);

void flux_red_set_timeout_msec (red_t r, int msec);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
