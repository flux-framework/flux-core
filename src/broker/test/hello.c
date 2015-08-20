#include <flux/core.h>
#include <czmq.h>
#include <stdio.h>

#include "hello.h"

#include "src/common/libtap/tap.h"

void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

static int hello_count = 0;
void hello_cb (hello_t *hello, void *arg)
{
    char *prefix = arg;
    ok (hello_complete (hello) == true,
        "%s: hello_complete returned true: %s", prefix,
        hello_get_nodeset (hello));
    hello_count++;
}

static int hello_request = 0;
void hello_request_cb (flux_t h, flux_msg_watcher_t *w,
                       const flux_msg_t *msg, void *arg)
{
    hello_t *hello = arg;
    int rank;
    int rc = hello_decode (msg, &rank);

    ok (rc == 0 && hello_recvmsg (hello, msg) == 0,
        "size=3: hello_recvmsg works, rank %d", rank);
    if (rank == 2)
        flux_msg_watcher_stop (h, w);
    hello_request++;
}

void check_codec (void)
{
    flux_msg_t *msg;
    int rank;

    ok ((msg = hello_encode (42)) != NULL,
        "hello_encode works");
    ok (hello_decode (msg, &rank) == 0 && rank == 42,
        "hello_decode works, returns encoded rank");
    flux_msg_destroy (msg);
}

void check_size1 (flux_t h)
{
    hello_t *hello;
    char *prefix = "size=1";

    hello_count = 0;
    ok ((hello = hello_create ()) != NULL,
        "%s: hello_create works", prefix);
    hello_set_flux (hello, h);
    hello_set_callback (hello, hello_cb, prefix);
    hello_set_timeout (hello, 0.1);
    ok (hello_start (hello) == 0,
        "%s: hello_start works", prefix);
    ok (hello_count == 1,
        "%s: hello callback was called once", prefix);

    hello_destroy (hello);
}

void check_size3 (flux_t h)
{
    hello_t *hello;
    flux_msg_watcher_t *w;
    uint32_t size = 3;
    uint32_t rank = 0;
    char *prefix = "size=3";

    flux_aux_set (h, "flux::size", &size, NULL);
    flux_aux_set (h, "flux::rank", &rank, NULL);

    hello_count = hello_request = 0;
    ok ((hello = hello_create ()) != NULL,
        "%s: hello_create works", prefix);
    hello_set_flux (hello, h);
    hello_set_callback (hello, hello_cb, prefix);
    hello_set_timeout (hello, 0.1);

    w = flux_msg_watcher_create (FLUX_MATCH_REQUEST, hello_request_cb, hello);
    ok (w != NULL,
        "%s: created cmb.hello watcher", prefix);
    flux_msg_watcher_start (h, w);

    flux_aux_set (h, "flux::rank", &rank, NULL);
    ok (hello_start (hello) == 0,
        "%s: (rank 0) hello_start works", prefix);

    rank = 1;
    flux_aux_set (h, "flux::rank", &rank, NULL);
    ok (hello_start (hello) == 0,
        "%s: (rank 1) hello_start works", prefix);

    rank = 2;
    flux_aux_set (h, "flux::rank", &rank, NULL);
    ok (hello_start (hello) == 0,
        "%s: (rank 2) hello_start works", prefix);

    ok (flux_reactor_start (h) == 0,
        "%s: flux reactor exited normally", prefix);

    flux_msg_watcher_destroy (w);
    hello_destroy (hello);
}

int main (int argc, char **argv)
{
    flux_t h;

    plan (2+1+4+9);

    check_codec (); // 2

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);

    check_size1 (h); // 4

    check_size3 (h); // 9

    flux_close (h);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
