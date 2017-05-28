#include <errno.h>
#include <czmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

static int send_request (flux_t *h, const char *topic)
{
    int rc = -1;
    flux_msg_t *msg = flux_request_encode (topic, NULL);
    if (!msg || flux_send (h, msg, 0) < 0) {
        fprintf (stderr, "%s: flux_send failed: %s",
                 __FUNCTION__, strerror (errno));
        goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int multmatch_count = 0;
static void multmatch1 (flux_t *h, flux_msg_handler_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.baz"))
        flux_reactor_stop_error (flux_get_reactor (h));
    flux_msg_handler_stop (w);
    multmatch_count++;
}

static void multmatch2 (flux_t *h, flux_msg_handler_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.bar"))
        flux_reactor_stop_error (flux_get_reactor (h));
    flux_msg_handler_stop (w);
    multmatch_count++;
}

static void test_multmatch (flux_t *h)
{
    flux_msg_handler_t *w1, *w2;
    struct flux_match m1 = FLUX_MATCH_ANY;
    struct flux_match m2 = FLUX_MATCH_ANY;

    m1.topic_glob = "foo.*";
    m2.topic_glob = "foo.bar";

    /* test #1: verify multiple match behaves as documented, that is,
     * a message is matched (only) by the most recently added watcher
     */
    ok ((w1 = flux_msg_handler_create (h, m1, multmatch1, NULL)) != NULL,
        "multmatch: first added handler for foo.*");
    ok ((w2 = flux_msg_handler_create (h, m2, multmatch2, NULL)) != NULL,
        "multmatch: next added handler for foo.bar");
    flux_msg_handler_start (w1);
    flux_msg_handler_start (w2);
    ok (send_request (h, "foo.bar") == 0,
        "multmatch: send foo.bar msg");
    ok (send_request (h, "foo.baz") == 0,
        "multmatch: send foo.baz msg");
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0 && multmatch_count == 2,
        "multmatch: last added watcher handled foo.bar");
    flux_msg_handler_destroy (w1);
    flux_msg_handler_destroy (w2);
}

static int msgwatcher_count = 100;
static void msgreader (flux_t *h, flux_msg_handler_t *w, const flux_msg_t *msg,
                       void *arg)
{
    static int count = 0;
    count++;
    if (count == msgwatcher_count)
        flux_msg_handler_stop (w);
}

static void test_msg (flux_t *h)
{
    flux_msg_handler_t *w;
    int i;

    ok ((w = flux_msg_handler_create (h, FLUX_MATCH_ANY, msgreader, NULL))
        != NULL,
        "msg: created handler for any message");
    flux_msg_handler_start (w);
    for (i = 0; i < msgwatcher_count; i++) {
        if (send_request (h, "foo") < 0)
            break;
    }
    ok (i == msgwatcher_count,
        "msg: sent %d requests", i);
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "msg: reactor ran to completion after %d requests", msgwatcher_count);
    flux_msg_handler_stop (w);
    flux_msg_handler_destroy (w);
}

static void dummy (flux_t *h, flux_msg_handler_t *w,
                   const flux_msg_t *msg, void *arg)
{
}

static void leak_msg_handler (void)
{
    flux_t *h;
    flux_msg_handler_t *w;

    if (!(h = flux_open ("loop://", 0)))
        exit (1);
    if (!(w = flux_msg_handler_create (h, FLUX_MATCH_ANY, dummy, NULL)))
        exit (1);
    flux_msg_handler_start (w);
    flux_close (h);
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);
    ok ((reactor = flux_get_reactor (h)) != NULL,
        "obtained reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_msg (h);
    test_multmatch (h);

    /* Misc
     */
    lives_ok ({ leak_msg_handler ();},
        "leaking a msg_handler_t doesn't segfault");

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

