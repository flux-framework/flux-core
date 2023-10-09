/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Test server - support brokerless testing
 *
 * Start a thread running the user-supplied function 'cb' which
 * is connected back to back to a flux_t handle returned by the
 * create function.  To finalize, call test_server_stop(),
 * followed by flux_close().
 *
 * Caveats:
 * 1) subscribe/unsubscribe requests are not supported
 * 2) all messages are sent with credentials userid=getuid(), rolemask=OWNER
 * 3) broker attributes (such as rank and size) are unavailable
 * 4) message nodeid is ignored
 *
 * If callback is NULL, a default callback is run that logs each
 * message received with diag().
 */
typedef int (*test_server_f)(flux_t *h, void *arg);

flux_t *test_server_create (int flags, test_server_f cb, void *arg);

int test_server_stop (flux_t *c);
