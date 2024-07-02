/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_REACTOR_H
#define _FLUX_CORE_REACTOR_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reactor
 */

typedef struct flux_reactor flux_reactor_t;

/* Flags for flux_reactor_run()
 */
enum {
    FLUX_REACTOR_NOWAIT = 1,  /* run loop once without blocking */
    FLUX_REACTOR_ONCE = 2,    /* same as above but block until at least */
                              /*     one event occurs */
};

/* Flags for flux_reactor_create()
 */
enum {
    FLUX_REACTOR_SIGCHLD = 1,  /* enable use of child watchers */
                               /*    only one thread can do this per program */
};

flux_reactor_t *flux_reactor_create (int flags);
void flux_reactor_destroy (flux_reactor_t *r);

flux_reactor_t *flux_get_reactor (flux_t *h);
int flux_set_reactor (flux_t *h, flux_reactor_t *r);

int flux_reactor_run (flux_reactor_t *r, int flags);

void flux_reactor_stop (flux_reactor_t *r);
void flux_reactor_stop_error (flux_reactor_t *r);

double flux_reactor_now (flux_reactor_t *r);
void flux_reactor_now_update (flux_reactor_t *r);
double flux_reactor_time (void);

/* Change reactor reference count.
 * Each active watcher holds a reference.
 * When the reference count reaches zero, the reactor loop exits.
 */
void flux_reactor_active_incref (flux_reactor_t *r);
void flux_reactor_active_decref (flux_reactor_t *r);


/* Watchers
 */

typedef struct flux_watcher flux_watcher_t;

typedef void (*flux_watcher_f)(flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg);

void flux_watcher_start (flux_watcher_t *w);
void flux_watcher_stop (flux_watcher_t *w);
void flux_watcher_destroy (flux_watcher_t *w);
double flux_watcher_next_wakeup (flux_watcher_t *w);
bool flux_watcher_is_active (flux_watcher_t *w);

/* flux_t handle
 */

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t *h, int events,
                                            flux_watcher_f cb, void *arg);
flux_t *flux_handle_watcher_get_flux (flux_watcher_t *w);

/* file descriptor
 */

flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r, int fd, int events,
                                        flux_watcher_f cb, void *arg);
int flux_fd_watcher_get_fd (flux_watcher_t *w);

/* timer
 */

flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                           double after, double repeat,
                                           flux_watcher_f cb, void *arg);

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat);

void flux_timer_watcher_again (flux_watcher_t *w);

/* periodic
 */

typedef double (*flux_reschedule_f) (flux_watcher_t *w, double now, void *arg);

flux_watcher_t *flux_periodic_watcher_create (flux_reactor_t *r,
                                             double offset, double interval,
                                             flux_reschedule_f reschedule_cb,
                                             flux_watcher_f cb, void *arg);

void flux_periodic_watcher_reset (flux_watcher_t *w,
                                  double next_wakeup, double interval,
                                  flux_reschedule_f reschedule_cb);


/* prepare/check/idle
 */

flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb, void *arg);

flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb, void *arg);

flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb, void *arg);

/* child
 */

flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                           int pid, bool trace,
                                           flux_watcher_f cb, void *arg);
int flux_child_watcher_get_rpid (flux_watcher_t *w);
int flux_child_watcher_get_rstatus (flux_watcher_t *w);

/* signal
 */

flux_watcher_t *flux_signal_watcher_create (flux_reactor_t *r, int signum,
                                            flux_watcher_f cb, void *arg);

int flux_signal_watcher_get_signum (flux_watcher_t *w);

/* stat
 */

flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                          const char *path, double interval,
                                          flux_watcher_f cb, void *arg);
void flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                  struct stat *stat, struct stat *prev);

/* Custom watcher construction functions:
 */

struct flux_watcher_ops {
    void (*start) (flux_watcher_t *w);
    void (*stop) (flux_watcher_t *w);
    void (*destroy) (flux_watcher_t *w);
};

/*  Create a custom watcher on reactor 'r' with 'data_size' bytes reserved
 *   for the implementor, implementation operations in 'ops' and user
 *   watcher callback and data 'fn' and 'arg'.
 *
 *  Caller retrieves pointer to allocated implementation data with
 *   flux_watcher_data (w).
 */
flux_watcher_t * flux_watcher_create (flux_reactor_t *r, size_t data_size,
                                      struct flux_watcher_ops *ops,
                                      flux_watcher_f fn, void *arg);

/*  Return pointer to implementation data reserved by watcher object 'w'.
 */
void * flux_watcher_get_data (flux_watcher_t *w);

/*  Return pointer to flux_watcher_ops structure for this watcher.
 */
struct flux_watcher_ops * flux_watcher_get_ops (flux_watcher_t *w);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
