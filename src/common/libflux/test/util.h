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
 * 2) all messages are sent with credentials userid=geteuid(), rolemask=OWNER
 * 3) broker attributes (such as rank and size) are unavailable
 * 4) message nodeid is ignored
 *
 * Unit tests that use the test server should call
 * test_server_environment_init() once prior to creating the first server
 * to initialize czmq's runtime.
 *
 * If callback is NULL, a default callback is run that logs each
 * message received with diag().
 */
typedef int (*test_server_f) (flux_t *h, void *arg);

flux_t *test_server_create (test_server_f cb, void *arg);

int test_server_stop (flux_t *c);

void test_server_environment_init (const char *test_name);

/* Create a loopback connector for testing.
 * The net effect is much the same as flux_open("local://") except
 * the implementation is self contained here.  Close with flux_close().
 *
 * Like loop://, this support test manipulation of credentials:
 *   flux_opt_set (h, FLUX_OPT_TESTING_USERID, &userid, sizeof (userid);
 *   flux_opt_set (h, FLUX_OPT_TESTING_ROLEMASK, &rolemask, sizeof (rolemask))
 *
 * N.B. No need to call test_server_environment_init() if this is the
 * only component used from this module.
 */
flux_t *loopback_create (int flags);
