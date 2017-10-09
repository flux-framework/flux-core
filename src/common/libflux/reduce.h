#ifndef _FLUX_CORE_REDUCE_H
#define _FLUX_CORE_REDUCE_H

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_reduce_struct flux_reduce_t;

struct flux_reduce_ops {
    flux_free_f destroy;
    void   (*reduce)(flux_reduce_t *r, int batchnum, void *arg);
    void   (*sink)(flux_reduce_t *r, int batchnum, void *arg);
    void   (*forward)(flux_reduce_t *r, int batchnum, void *arg);
    int    (*itemweight)(void *item);
};

enum {
    FLUX_REDUCE_TIMEDFLUSH = 1,
    FLUX_REDUCE_HWMFLUSH = 2,
};

enum {
    FLUX_REDUCE_OPT_TIMEOUT = 1,
    FLUX_REDUCE_OPT_HWM = 2,
    FLUX_REDUCE_OPT_COUNT = 3,
    FLUX_REDUCE_OPT_WCOUNT = 4,
};

flux_reduce_t *flux_reduce_create (flux_t *h, struct flux_reduce_ops ops,
                                   double timeout, void *arg, int flags);

void flux_reduce_destroy (flux_reduce_t *r);

int flux_reduce_append (flux_reduce_t *r, void *item, int batchnum);

void *flux_reduce_pop (flux_reduce_t *r);

int flux_reduce_push (flux_reduce_t *r, void *item);

int flux_reduce_opt_get (flux_reduce_t *r, int option, void *val, size_t size);

int flux_reduce_opt_set (flux_reduce_t *r, int option, void *val, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _FLUX_CORE_REDUCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
