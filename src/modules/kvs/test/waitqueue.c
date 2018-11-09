#include "src/modules/kvs/waitqueue.h"
#include "src/common/libflux/message.h"
#include "src/common/libtap/tap.h"

void wait_cb (void *arg)
{
    int *count = arg;
    (*count)++;
}

void error_cb (wait_t *w, int errnum, void *arg)
{
    int *err = arg;
    (*err) = errnum;
}

void iter_cb (wait_t *w, void *arg)
{
    int *count = arg;
    (*count)++;

    /* what "foobar" is set to not important, just set to count */
    ok (wait_msg_aux_set (w, "foobar", count, NULL) == 0,
        "wait_msg_aux_set works");
}

void msghand (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    int *count = arg;
    (*count)++;
}

bool msgcmp (const flux_msg_t *msg, void *arg)
{
    char *id = NULL;
    bool match = false;
    if (flux_msg_get_route_first (msg, &id) == 0
        && (!strcmp (id, "19") || !strcmp (id, "18") || !strcmp (id, "17")))
        match = true;
    if (id)
        free (id);
    return match;
}

bool msgcmp2 (const flux_msg_t *msg, void *arg)
{
    return true;
}

int main (int argc, char *argv[])
{
    waitqueue_t *q;
    waitqueue_t *q2;
    wait_t *w, *w1, *w2;
    flux_msg_t *msg, *msg1, *msg2;
    char *str;
    int count;
    int errnum;
    int i;

    plan (NO_PLAN);

    wait_destroy (NULL);
    wait_queue_destroy (NULL);
    diag ("wait_destroy and wait_queue_destroy accept NULL args");

    /* Create/destroy wait_t
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    wait_destroy (w);
    ok (count == 0,
        "wait_destroy didn't run callback");

    /* corner case checks */

    ok (wait_msg_aux_set (NULL, NULL, NULL, NULL) < 0,
        "wait_msg_aux_set returns -1 on bad input");
    ok (!wait_msg_aux_get (NULL, NULL),
        "wait_msg_aux_set returns NULL on bad input");
    ok (wait_aux_get_errnum (NULL) < 0,
        "wait_aux_get_errnum returns -1 on bad input");

    /* Create/destroy wait_t with msg handler, and set/get aux data
     */
    count = 0;
    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    ok (msg != NULL,
        "flux_msg_create works");
    w = wait_create_msg_handler (NULL, NULL, msg, &count, msghand);
    ok (w != NULL,
        "wait_create_msg_handler with non-NULL msg works");
    ok (wait_msg_aux_set (w, "aux", "val", NULL) == 0,
        "wait_msg_aux_set works");
    str = wait_msg_aux_get (w, "aux");
    ok (str && !strcmp (str, "val"),
        "wait_msg_aux_get works and returns correct value");
    flux_msg_destroy (msg);
    wait_destroy (w);
    ok (count == 0,
        "wait_destroy didn't run callback");

    /* Create/destroy wait_t, and set/run error cb
     */
    errnum = 0;
    ok ((w = wait_create (wait_cb, NULL)) != NULL,
        "wait_create works");
    ok (wait_aux_get_errnum (w) == 0,
        "wait_aux_get_errnum returns 0 initially");
    ok (wait_set_error_cb (w, error_cb, &errnum) == 0,
        "wait_set_error_cb works");
    ok (wait_aux_set_errnum (w, ENOTSUP) == 0,
        "wait_aux_set_errnum works");
    ok (errnum == ENOTSUP,
        "error cb called correctly");
    ok (wait_aux_get_errnum (w) == ENOTSUP,
        "wait_aux_get_errnum returns errnum correctly");
    wait_destroy (w);

    /* Create/destroy waitqueue_t with msgs, iterate over them */
    count = 0;
    msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    ok (msg1 != NULL,
        "flux_msg_create works");
    msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    ok (msg2 != NULL,
        "flux_msg_create works");
    w1 = wait_create_msg_handler (NULL, NULL, msg1, NULL, NULL);
    ok (w1 != NULL,
        "wait_create_msg_handler works");
    w2 = wait_create_msg_handler (NULL, NULL, msg1, NULL, NULL);
    ok (w2 != NULL,
        "wait_create_msg_handler works");
    q = wait_queue_create ();
    ok (q != NULL,
        "wait_queue_create works");
    ok (wait_addqueue (q, w1) == 0,
        "wait_addqueue works");
    ok (wait_addqueue (q, w2) == 0,
        "wait_addqueue works");
    ok (wait_queue_iter (q, iter_cb, &count) == 0,
        "wait_queue_iter works");
    ok (count == 2,
        "wait_queue_iter iterated the correct number of times");
    ok (wait_msg_aux_get (w1, "foobar") != NULL,
        "wait_queue_iter callback set aux correctly");
    ok (wait_msg_aux_get (w2, "foobar") != NULL,
        "wait_queue_iter callback set aux correctly");
    wait_queue_destroy (q);
    flux_msg_destroy (msg1);
    flux_msg_destroy (msg2);

    /* Create wait_t, add to queue, run queue, destroy queue.
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((q = wait_queue_create ()) != NULL,
        "wait_queue_create works");
    ok (wait_addqueue (q, w) == 0,
        "wait_addqueue works");
    ok (wait_get_usecount (w) == 1,
        "wait_get_usecount 1 after wait_addqueue");
    ok (count == 0,
        "wait_t callback not run");
    ok (wait_runqueue (q) == 0,
        "wait_runqueue success");
    ok (count == 1,
        "wait_runqueue ran callback");
    wait_queue_destroy (q);

    /**
     ** msg_handler
     **/

    q = wait_queue_create ();
    q2 = wait_queue_create ();
    ok (q && q2,
        "wait_queue_create works");
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 0,
        "wait_queue_length 0 on new queue");

    /* Create wait_t for msg
     * Add to two queues, run queues, wait_t called once
     */
    count = 0;
    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    ok (msg != NULL,
        "flux_msg_create works");
    w = wait_create_msg_handler (NULL, NULL, msg, &count, msghand);
    ok (w != NULL,
        "wait_create_msg_handler with non-NULL msg works");
    flux_msg_destroy (msg);

    ok (wait_get_usecount (w) == 0,
        "wait_usecount 0 initially");
    ok (wait_addqueue (q, w) == 0,
        "wait_addqueue works");
    ok (wait_get_usecount (w) == 1,
        "wait_usecount 1 after adding to one queue");
    ok (wait_addqueue (q2, w) == 0,
        "wait_addqueue works");
    ok (wait_get_usecount (w) == 2,
        "wait_usecount 2 after adding to second queue");
    ok (wait_queue_length (q) == 1 && wait_queue_length (q2) == 1,
        "wait_queue_length of each queue is 1");

    ok (wait_runqueue (q) == 0,
        "wait_runqueue success");
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 1,
        "wait_runqueue dequeued wait_t from first queue");
    ok (wait_get_usecount (w) == 1,
        "wait_usecount 1 after one run");
    ok (count == 0,
        "wait_t callback has not run");

    ok (wait_runqueue (q2) == 0,
        "wait_runqueue success");
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 0,
        "wait_runqueue dequeued wait_t from second queue");
    ok (count == 1,
        "wait_t callback has run");

    /* Add 20 waiters to queue, selectively destroy, callbacks not run
     */

    count = 0;
    for (i = 0; i < 20; i++) {
        char s[16];
        snprintf (s, sizeof (s), "%d", i);
        if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
            break;
        if (flux_msg_enable_route (msg) < 0 || flux_msg_push_route (msg, s) < 0)
            break;
        if (!(w = wait_create_msg_handler (NULL, NULL, msg, &count, msghand)))
            break;
        flux_msg_destroy (msg); /* msg was copied into wait_t */
        if (wait_addqueue (q, w) < 0)
            break;
    }
    ok (wait_queue_length (q) == 20,
        "wait_queue_length 20 after 20 wait_addqueues");
    ok (count == 0,
        "wait_t callback has not run");

    ok ((i = wait_destroy_msg (q, msgcmp, NULL)) == 3,
        "wait_destroy_msg found 3 matches");
    ok (wait_queue_length (q) == 17,
        "wait_queue_length 17 after 3 deletions");
    ok (count == 0,
        "wait_t callback has not run");

    ok ((i = wait_destroy_msg (q, msgcmp2, NULL)) == 17,
        "wait_destroy_msg found 17 matches");
    ok (wait_queue_length (q) == 0,
        "wait_queue_length 0 after 17 deletions");
    ok (count == 0,
        "wait_t callback has not run");

    wait_queue_destroy (q);
    wait_queue_destroy (q2);

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

