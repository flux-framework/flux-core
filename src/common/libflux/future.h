#ifndef _FLUX_CORE_FUTURE_H
#define _FLUX_CORE_FUTURE_H

#include "reactor.h"
#include "types.h"
#include "handle.h"
#include "msg_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Interfaces useful for all classes that return futures.
 * See flux_future_then(3).
 */

typedef struct flux_future flux_future_t;

typedef void (*flux_continuation_f)(flux_future_t *f, void *arg);

int flux_future_then (flux_future_t *f, double timeout,
                      flux_continuation_f cb, void *arg);

int flux_future_wait_for (flux_future_t *f, double timeout);

bool flux_future_is_ready (flux_future_t *f);

void flux_future_reset (flux_future_t *f);

void flux_future_destroy (flux_future_t *f);

void *flux_future_aux_get (flux_future_t *f, const char *name);
int flux_future_aux_set (flux_future_t *f, const char *name,
                         void *aux, flux_free_f destroy);

/* Functions primarily used by implementors of classes that return futures.
 * See flux_future_create(3).
 */

typedef void (*flux_future_init_f)(flux_future_t *f, void *arg);

flux_future_t *flux_future_create (flux_future_init_f cb, void *arg);

int flux_future_get (flux_future_t *f, void *result);

void flux_future_fulfill (flux_future_t *f, void *result, flux_free_f free_fn);
void flux_future_fulfill_error (flux_future_t *f, int errnum);

void flux_future_set_flux (flux_future_t *f, flux_t *h);
flux_t *flux_future_get_flux (flux_future_t *f);

void flux_future_set_reactor (flux_future_t *f, flux_reactor_t *r);
flux_reactor_t *flux_future_get_reactor (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_FUTURE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
