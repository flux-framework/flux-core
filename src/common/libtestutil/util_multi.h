/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _TESTUTIL_UTIL_MULTI_H
#define _TESTUTIL_UTIL_MULTI_H

#include <flux/core.h>

/* Multi-rank test server: like test_server_create() in util.h, but
 * simulating multiple broker ranks with an internal reactor and message router.  Instead of a callback
 * in which a test user runs a reactor loop, the callback is a setup function
 * that performs test-specific setup, like registering message handlers, and
 * attaches them to the per-rank flux_t handle's aux container.  Once 'size'
 * setup callbacks return, the internal reactor runs until test_server_stop().
 *
 * The returned handle is positioned where a client of the rank 0 broker
 * sits, and each rank's handle has its "rank" and "size" attributes set
 * before 'cb' runs:
 * - nodeid=<rank> is handled by that rank
 * - nodeid=FLUX_NODEID_ANY is handled by rank 0
 * - nodeid=FLUX_NODEID_UPSTREAM fails with EHOSTUNREACH: rank 0 has no parent
 * - a nodeid of 'size' or greater fails with EHOSTUNREACH: unroutable
 *
 * Caveats:
 * 1) subscribe/unsubscribe requests are not supported
 * 2) all messages are sent with credentials userid=getuid(), rolemask=OWNER
 * 3) broker attributes other than rank and size are unavailable
 * 4) the topic string "shutdown" is reserved: the harness stops when it
 *    arrives rather than routing it to a rank
 *
 * Note: this test architecture is, by design, compatible with spawning a
 * libsubprocess server per rank.  The alternate approach of thread-per-rank
 * and callback that runs the reactor would not be, due to libsubprocess's
 * use of a refcounted SIGCHLD handler that is global to the process and
 * bound to the reactor that initializes it first, so with a thread per
 * rank, children of every rank would be reaped on one thread, invoking
 * callbacks that touch other threads' server state.  For the same reason, a
 * test that spawns a subprocess on its own thread before calling this binds
 * the handler elsewhere, and ranks here will not reap children.
 *
 * To finalize, call test_server_stop_multi(), followed by flux_close().
 */
typedef int (*test_server_setup_f)(flux_t *h, void *arg);

flux_t *test_server_create_multi (int size,
                                  int flags,
                                  test_server_setup_f cb,
                                  void *arg);


int test_server_stop_multi (flux_t *c);

#endif /* !_TESTUTIL_UTIL_MULTI_H */

// vi:ts=4 sw=4 expandtab
