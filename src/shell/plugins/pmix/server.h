/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PMIX_PP_SERVER_H
#define _PMIX_PP_SERVER_H

struct psrv *pp_server_create (flux_reactor_t *r,
                               const char *tmpdir,
                               pmix_server_module_t *callbacks,
                               pmix_notification_fn_t error_cb,
                               void *arg);

void pp_server_destroy (struct psrv *psrv);

pmix_status_t pp_server_dmodex_request (struct psrv *psrv,
                                        const pmix_proc_t *proc,
                                        pmix_dmodex_response_fn_t cbfunc,
                                        void *cbdata);

#endif

// vi:ts=4 sw=4 expandtab
