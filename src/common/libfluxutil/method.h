/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_METHOD_H
#define _FLUX_CORE_METHOD_H

#include <flux/core.h>

/* The ping method requires the server uuid to be stored as a string in
 * the flux_t aux container under the "flux::uuid" key.
 */
void method_ping_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg);

void method_rusage_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg);

void method_stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg);

void method_stats_clear_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg);

void method_stats_clear_event_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg);
void method_config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg);



#endif /* !_FLUX_CORE_METHOD_H */

// vi:ts=4 sw=4 expandtab
