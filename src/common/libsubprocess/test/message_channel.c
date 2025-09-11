/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <assert.h>
#include <flux/core.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/subprocess_private.h"
#include "src/common/libsubprocess/server.h"
#include "ccan/str/str.h"

extern char **environ;

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

void completion_cb (flux_subprocess_t *p)
{
    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED,
        "subprocess state == EXITED in completion handler");
    ok (flux_subprocess_status (p) != -1,
        "subprocess status is valid");
    ok (flux_subprocess_exit_code (p) == 0,
        "subprocess exit code is 0, got %d", flux_subprocess_exit_code (p));
}

void message_cb (flux_reactor_t *r,
                 flux_watcher_t *w,
                 int revents,
                 void *arg)
{
    int *count = arg;
    flux_t *h = flux_handle_watcher_get_flux (w);
    static int recvcount = 0;
    flux_msg_t *msg;

    if (!(msg = flux_recv (h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            diag ("flux_recv: %s", strerror (errno));
            flux_reactor_stop_error (r);
        }
        return;
    }
    flux_msg_decref (msg);
    recvcount++;
    if (recvcount == *count) {
        flux_reactor_stop (r);
    }

}

void unique_interthread_uri (char *buf, size_t size)
{
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);
    snprintf (buf, size, "interthread://%s", uuid_str);
}

void test_message_channel (flux_reactor_t *r, int count)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echomsg", NULL };
    int ac = 1;
    flux_cmd_t *cmd;
    char uri[118];
    flux_subprocess_t *p = NULL;
    flux_msg_t *testmsg = flux_request_encode ("foo", NULL);
    flux_msg_t *donemsg = flux_request_encode ("done", NULL);
    flux_t *h;
    flux_watcher_t *w;
    int i;

    if (!testmsg || !donemsg)
        BAIL_OUT ("could not create test messages");
    if (!(cmd = flux_cmd_create (ac, av, environ)))
        BAIL_OUT ("flux_cmd_create failed");

    unique_interthread_uri (uri, sizeof (uri));
    if (!(h = flux_open (uri, 0)))
        BAIL_OUT ("could not open %s", uri);
    flux_set_reactor (h, r);
    if (!(w = flux_handle_watcher_create (r,
                                          h,
                                          FLUX_POLLIN,
                                          message_cb,
                                          &count)))
        BAIL_OUT ("could not create handle watcher");
    flux_watcher_start (w);

    ok (flux_cmd_add_message_channel (cmd, "TEST_URI", uri) == 0,
        "flux_cmd_add_message_channel TEST_URI %s works", uri);

    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_stdout = subprocess_standard_output,
        .on_stderr = subprocess_standard_output
    };
    p = flux_local_exec (r, 0, cmd, &ops);
    ok (p != NULL,
        "flux_local_exec test_echomsg works");

    ok (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING,
        "subprocess state is RUNNING");

    /* Send all the messages on the interthread handle.
     * It just expands to fit.
     */
    for (i = 0; i < count; i++) {
        if (flux_send (h, testmsg, 0) < 0) {
            diag ("flux_send: %s", strerror (errno));
            break;
        }
    }
    ok (i == count,
        "sent %d test messages", count);
    ok (flux_send (h, donemsg, 0) == 0,
        "sent end sentinel message");

    /* Run the reactor so the consumer can read back echoed messages.
     * After 'count' are received, it stops the reactor nicely.
     */
    int rc = flux_reactor_run (r, 0);
    ok (rc >= 0,
        "all the messages were echoed back");

    flux_subprocess_destroy (p);
    flux_cmd_destroy (cmd);
    flux_msg_destroy (testmsg);
    flux_msg_destroy (donemsg);
    flux_watcher_destroy (w);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *r;

    plan (NO_PLAN);

    int start_fdcount = fdcount ();

    ok ((r = flux_reactor_create (0)) != NULL,
        "flux_reactor_create");

    test_message_channel (r, 1000);

    flux_reactor_destroy (r);

    int end_fdcount = fdcount ();

    ok (start_fdcount == end_fdcount,
        "no file descriptors leaked");

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
