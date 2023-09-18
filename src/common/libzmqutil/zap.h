/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include <czmq.h>
#include <zmq.h>

struct zmqutil_zap;

typedef void (*zaplog_f)(int severity, const char *message, void *arg);

int zmqutil_zap_authorize (struct zmqutil_zap *zap,
                           const char *name,
                           const char *pubkey);

void zmqutil_zap_set_logger (struct zmqutil_zap *zap, zaplog_f fun, void *arg);

void zmqutil_zap_destroy (struct zmqutil_zap *zap);
struct zmqutil_zap *zmqutil_zap_create (void *zctx, flux_reactor_t *r);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
