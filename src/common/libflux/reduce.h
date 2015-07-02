#ifndef _FLUX_CORE_REDUCE_H
#define _FLUX_CORE_REDUCE_H

#include "handle.h"

typedef struct flux_red_struct *flux_red_t;
typedef struct flux_redstack_struct *flux_redstack_t;
typedef void   (*flux_red_f)(flux_t h, flux_redstack_t stack,
                             int batchnum, void *arg);
typedef void   (*flux_sink_f)(flux_t h, void *item, int batchnum, void *arg);

enum {
    FLUX_RED_TIMEDFLUSH = 1,// flush timer starts on first append when empty

    FLUX_RED_HWMFLUSH = 2,  // initially every append is flushed; after
                            //   batchnum is incremented, hwm is calculated
                            //   from previous batch and next flush is at hwm
};


/* Create/destroy a reduction handle.
 * The sink function will be called every time the handle is flushed.
 * Flush occurs according to reduction flags (see defs above).
 * If no flags, flush will be called after every flux_red_append().
 */
flux_red_t flux_red_create (flux_t h, flux_sink_f sinkfn, void *arg);
void flux_red_destroy (flux_red_t r);

/* Set the reduction function (optional).
 * Reduction function will be called every time an item is appended.
 */
void flux_red_set_reduce_fn (flux_red_t r, flux_red_f redfn);

/* Set reduction flags.
 */
void flux_red_set_flags (flux_red_t r, int flags);

/* Set the timeout value used with FLUX_RED_TIMEDFLUSH
 */
void flux_red_set_timeout_msec (flux_red_t r, int msec);

/* Map the sink function over each item in the handle.
 */
void flux_red_flush (flux_red_t r);

/* Append an item to the reduction handle.
 * The reduction function is immediately called (if defined).
 * The sink function is called according to flags.
 * Returns 0 on success, -1 on failure.
 */
int flux_red_append (flux_red_t r, void *item, int batchnum);

/* Simple stack ops for stack of items passed to flux_red_f.
 */
void *flux_redstack_pop (flux_redstack_t stack);
void flux_redstack_push (flux_redstack_t stack, void *item);
int flux_redstack_count (flux_redstack_t stack);

#endif /* _FLUX_CORE_REDUCE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
