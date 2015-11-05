#include "src/modules/kvs/waitqueue.h"
#include "src/common/libflux/message.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

void msghand (flux_t h, flux_msg_handler_t *w, const flux_msg_t *msg, void *arg)
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
    wait_t *w;
    flux_msg_t *msg;
    int count = 0;
    int i;

    plan (NO_PLAN);

    q = wait_queue_create ();
    q2 = wait_queue_create ();
    ok (q && q2,
        "wait_queue_create works");
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 0,
        "wait_queue_length on brandnew waitqueue_t returns zero");

    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    ok (msg != NULL,
        "flux_msg_create works");
    w = wait_create (NULL, NULL, msg, msghand, &count);
    ok (w != NULL,
        "wait_create works");
    flux_msg_destroy (msg);

    wait_addqueue (q, w);
    wait_addqueue (q2, w);
    ok (wait_queue_length (q) == 1 && wait_queue_length (q2) == 1,
        "wait_addqueue can add wait_t to a two queues");
    wait_runqueue (q);
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 1,
        "wait_runqueue dequeued wait_t from first queue");
    wait_runqueue (q2);
    ok (wait_queue_length (q) == 0 && wait_queue_length (q2) == 0,
        "wait_runqueue dequeued wait_t from second queue");
    ok (count == 1,
        "wait_runqueue ran the wait_t once");

    for (i = 0; i < 20; i++) {
        char *s = xasprintf ("%d", i);
        msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);
        if (!msg)
            break;
        if (flux_msg_enable_route (msg) < 0
            || flux_msg_push_route (msg, s) < 0)
            break;
        w = wait_create (NULL, NULL, msg, msghand, &count);
        if (!w)
            break;
        wait_addqueue (q, w);
        free (s);
    }
    ok (wait_queue_length (q) == 20,
        "wait_addqueue 20x works");
    wait_destroy_match (q, msgcmp, NULL);
    ok (wait_queue_length (q) == 17,
        "wait_destroy_match on sender works");
    wait_destroy_match (q, msgcmp2, NULL);
    ok (wait_queue_length (q) == 0,
        "all-match wait_destroy_match works");

    wait_queue_destroy (q);
    wait_queue_destroy (q2);

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

