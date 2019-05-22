/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_FUTURE_H
#define _FLUX_CORE_FUTURE_H

#include "reactor.h"
#include "types.h"
#include "handle.h"
#include "msg_handler.h"
#include "flog.h"

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

int flux_future_get (flux_future_t *f, const void **result);

void flux_future_fulfill (flux_future_t *f, void *result, flux_free_f free_fn);
void flux_future_fulfill_error (flux_future_t *f, int errnum, const char *errstr);

int flux_future_fulfill_with (flux_future_t *f, flux_future_t *p);

void flux_future_fatal_error (flux_future_t *f, int errnum, const char *errstr);

/* Convenience macro */
#define future_strerror(__f, __errno) \
    (flux_future_has_error ((__f)) ? \
     flux_future_error_string ((__f)) : \
     flux_strerror ((__errno)))

bool flux_future_has_error (flux_future_t *f);
const char *flux_future_error_string (flux_future_t *f);

void flux_future_set_flux (flux_future_t *f, flux_t *h);
flux_t *flux_future_get_flux (flux_future_t *f);

void flux_future_set_reactor (flux_future_t *f, flux_reactor_t *r);
flux_reactor_t *flux_future_get_reactor (flux_future_t *f);

void flux_future_incref (flux_future_t *f);
void flux_future_decref (flux_future_t *f);

/* Composite future implementation
 */
flux_future_t *flux_future_wait_all_create (void);
flux_future_t *flux_future_wait_any_create (void);

int flux_future_push (flux_future_t *cf, const char *name, flux_future_t *f);

const char * flux_future_first_child (flux_future_t *cf);
const char * flux_future_next_child (flux_future_t *cf);

flux_future_t *flux_future_get_child (flux_future_t *cf, const char *name);

/* Future chaining
 */

/* Similar to flux_future_then(3), but return a future that can subsequently
 *  be "continued" from the continuation function `cb` via
 *  flux_future_continue(3) upon successful fulfillment of future `f`.
 *
 * The continuation `cb` is only called on success of future `f`. If `f`
 *  is fulfilled with an error, then that error is immediately passed
 *  to  future returned by this function, unless `flux_future_or_then(3)`
 *  has been used.
 */
flux_future_t *flux_future_and_then (flux_future_t *f,
                                     flux_continuation_f cb, void *arg);

/* Like flux_future_and_then(3), but run the continuation `cb` when
 *  future `f` is fulfilled with an error.
 */
flux_future_t *flux_future_or_then (flux_future_t *f,
                                    flux_continuation_f cb, void *arg);

/* Set the next future for the chained future `prev` to `f`.
 *  This function steals a reference to `f` and thus flux_future_destroy()
 *  should not be called on `f`. (However, flux_future_destroy() should
 *  still be called on `prev`)
 */
int flux_future_continue (flux_future_t *prev, flux_future_t *f);

/*  Set the next future for the chained future `prev` to be fulfilled
 *   with an error `errnum` and an optional error string.
 */
void flux_future_continue_error (flux_future_t *prev, int errnum,
                                 const char *errstr);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_FUTURE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
