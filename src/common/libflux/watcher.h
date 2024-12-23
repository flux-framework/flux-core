/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_WATCHER_H
#define _FLUX_CORE_WATCHER_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flux_watcher_f)(flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg);

/* Set the watcher priority.  The range is [-2:2] (default 0).
 * Higher priority watchers run first.
 * This is a no-op if the underlying watcher doesn't support it.
 * If the priority is out of range, the max or min value is set.
 * The priority should only be set when the watcher is stopped.
 * Currently only the check watcher supports it.
 */
void flux_watcher_set_priority (flux_watcher_t *w, int priority);

void flux_watcher_start (flux_watcher_t *w);
void flux_watcher_stop (flux_watcher_t *w);
void flux_watcher_ref (flux_watcher_t *w);
void flux_watcher_unref (flux_watcher_t *w);
void flux_watcher_destroy (flux_watcher_t *w);
double flux_watcher_next_wakeup (flux_watcher_t *w);
bool flux_watcher_is_active (flux_watcher_t *w);
bool flux_watcher_is_referenced (flux_watcher_t *w);

/* flux_t handle
 */

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t *h,
                                            int events,
                                            flux_watcher_f cb,
                                            void *arg);
flux_t *flux_handle_watcher_get_flux (flux_watcher_t *w);

/* file descriptor
 */

flux_watcher_t *flux_fd_watcher_create (flux_reactor_t *r,
                                        int fd, int events,
                                        flux_watcher_f cb,
                                        void *arg);
int flux_fd_watcher_get_fd (flux_watcher_t *w);

/* timer
 */

flux_watcher_t *flux_timer_watcher_create (flux_reactor_t *r,
                                           double after,
                                           double repeat,
                                           flux_watcher_f cb,
                                           void *arg);

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat);

void flux_timer_watcher_again (flux_watcher_t *w);

/* periodic
 */

typedef double (*flux_reschedule_f) (flux_watcher_t *w, double now, void *arg);

flux_watcher_t *flux_periodic_watcher_create (flux_reactor_t *r,
                                             double offset,
                                             double interval,
                                             flux_reschedule_f reschedule_cb,
                                             flux_watcher_f cb,
                                             void *arg);

void flux_periodic_watcher_reset (flux_watcher_t *w,
                                  double next_wakeup,
                                  double interval,
                                  flux_reschedule_f reschedule_cb);


/* prepare/check/idle
 */

flux_watcher_t *flux_prepare_watcher_create (flux_reactor_t *r,
                                             flux_watcher_f cb,
                                             void *arg);

flux_watcher_t *flux_check_watcher_create (flux_reactor_t *r,
                                           flux_watcher_f cb,
                                           void *arg);

flux_watcher_t *flux_idle_watcher_create (flux_reactor_t *r,
                                          flux_watcher_f cb,
                                          void *arg);

/* child
 */

flux_watcher_t *flux_child_watcher_create (flux_reactor_t *r,
                                           int pid,
                                           bool trace,
                                           flux_watcher_f cb,
                                           void *arg);
int flux_child_watcher_get_rpid (flux_watcher_t *w);
int flux_child_watcher_get_rstatus (flux_watcher_t *w);

/* signal
 */

flux_watcher_t *flux_signal_watcher_create (flux_reactor_t *r,
                                            int signum,
                                            flux_watcher_f cb,
                                            void *arg);

int flux_signal_watcher_get_signum (flux_watcher_t *w);

/* stat
 */

flux_watcher_t *flux_stat_watcher_create (flux_reactor_t *r,
                                          const char *path,
                                          double interval,
                                          flux_watcher_f cb,
                                          void *arg);
int flux_stat_watcher_get_rstat (flux_watcher_t *w,
                                 struct stat *stat,
                                 struct stat *prev);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_WATCHER_H */

// vi:ts=4 sw=4 expandtab
