#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/oom.h"
#include "src/common/libtap/tap.h"

int reduce_calls = 0;
int reduce_items = 0;
void reduce (flux_reduce_t *r, int batchnum, void *arg)
{
    void *item;
    zlist_t *tmp = zlist_new ();

    if (!tmp)
        oom ();
    reduce_calls++;
    while ((item = flux_reduce_pop (r))) {
        if (zlist_push (tmp, item) < 0)
            oom ();
        reduce_items++;
    }
    while ((item = zlist_pop (tmp))) {
        if (flux_reduce_push (r, item) < 0)
            oom ();
    }
    zlist_destroy (&tmp);
}

int sink_calls = 0;
int sink_items = 0;
void sink (flux_reduce_t *r, int batchnum, void *arg)
{
    void *item;

    sink_calls++;
    while ((item = flux_reduce_pop (r))) {
        free (item);
        sink_items++;
    }
}

int forward_calls = 0;
int forward_items = 0;
void forward (flux_reduce_t *r, int batchnum, void *arg)
{
    void *item;

    forward_calls++;
    while ((item = flux_reduce_pop (r))) {
        free (item);
        forward_items++;
    }
}

void clear_counts (void)
{
    sink_calls = sink_items = reduce_calls = reduce_items
                            = forward_calls = forward_items = 0;
}

int itemweight (void *item)
{
    return 1;
}

static struct flux_reduce_ops reduce_ops =  {
    .destroy = free,
    .reduce = reduce,
    .sink = sink,
    .forward = forward,
    .itemweight = itemweight,
};

void test_hwm (flux_t *h)
{
    flux_reduce_t *r;
    int i, errors;
    unsigned int hwm;

    clear_counts ();

    ok ((r = flux_reduce_create (h, reduce_ops, 0., NULL,
        FLUX_REDUCE_HWMFLUSH)) != NULL,
        "hwm: flux_reduce_create works");

    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 0,
        "hwm: hwm is initially zero");

    /* batch 0 is a training batch.
     * It looks just like no policy.
     */
    errors = 0;
    for (i = 0; i < 100; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 0) < 0)
            errors++;
    }
    ok (errors == 0,
        "hwm.0: flux_reduce_append added 100 items");
    cmp_ok (reduce_calls, "==", 0,
        "hwm.0: op.reduce not called (training)");
    cmp_ok (sink_calls, "==", 100,
        "hwm.0: op.sink called 100 times");
    cmp_ok (sink_items, "==", 100,
        "hwm.0: op.sink processed 100 items");

    clear_counts ();

    /* batch 1 has a hwm.  Put in one short of hwm items.
     */
    errors = 0;
    for (i = 0; i < 99; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 1) < 0)
            errors++;
    }
    ok (errors == 0,
        "hwm.1: flux_reduce_append added 99 items");
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 100,
        "hwm.0: hwm is 100");
    cmp_ok (reduce_calls, "==", 98,
        "hwm.1: op.reduce called 98 times");
    cmp_ok (sink_calls, "==", 0,
        "hwm.1: op.sink not called yet");

    /* Now finish batch 1 with one item.  Everything should go thru.
     */
    ok (flux_reduce_append (r, xstrdup ("hi"), 1) == 0,
        "hwm.1: flux_reduce_append added 1 item");
    cmp_ok (reduce_calls, "==", 99,
        "hwm.1: op.reduce called");
    cmp_ok (sink_calls, "==", 1,
        "hwm.1: op.sink called 1 time");
    cmp_ok (sink_items, "==", 100,
        "hwm.1: op.sink handled 100 items");
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 100,
        "hwm.1: hwm is 100");

    clear_counts ();

    /* Straggler test
     * Start batch 2, then append one item from batch 1.
     * This should cause last hwm to be recomputed to be 101 instead of 100.
     * Straggler should immediately be sinked.
     */
    ok (flux_reduce_append (r, xstrdup ("hi"), 2) == 0,
        "hwm.2: flux_reduce_append added 1 item");
    cmp_ok (reduce_calls, "==", 0,
        "hwm.2: op.reduce not called");
    cmp_ok (sink_calls, "==", 0,
        "hwm.2: op.sink not called");
    ok (flux_reduce_append (r, xstrdup ("hi"), 1) == 0,
        "hwm.1: flux_reduce_append added 1 straggler");
    cmp_ok (reduce_calls, "==", 0,
        "hwm.1: op.reduce not called");
    cmp_ok (sink_calls, "==", 1,
        "hwm.1: op.sink called 1 time");
    cmp_ok (sink_items, "==", 1,
        "hwm.1: op.sink handled 1 item");
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 101,
        "hwm.1: hwm is 101");

    sink_items = sink_calls = 0; // don't count batch 1 straggler below

    /* At this point we have one batch 2 item in queue.
     * Put in 99 more and we should be one short of 101 hwm.
     */
    errors = 0;
    for (i = 0; i < 99; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 2) < 0)
            errors++;
    }
    ok (errors == 0,
        "hwm.2: flux_reduce_append added 99 items");
    cmp_ok (reduce_calls, "==", 99,
        "hwm.2: op.reduce called 99 times");
    cmp_ok (sink_calls, "==", 0,
        "hwm.2: op.sink not called yet");
    ok (flux_reduce_append (r, xstrdup ("hi"), 2) == 0,
        "hwm.2: flux_reduce_append added 1 item");
    cmp_ok (sink_calls, "==", 1,
        "hwm.2: op.sink called 1 time");
    cmp_ok (sink_items, "==", 101,
        "hwm.2: op.sink handled 101 items");
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 101,
        "hwm.2: hwm is 101");

    clear_counts ();

    /* Manually set the hwm to 10.
     * Append 20 items to batch 3.
     * Reduce is called on the first set of 10.
     * The second set of 10 will be immediately flushed.
     * Put in one batch 4 item and verify the HWM is still 10.
     */
    hwm = 10;
    ok (flux_reduce_opt_set (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0,
        "hwm.3: hwm set to 10");
    errors = 0;
    for (i = 0; i < 20; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 3) < 0)
            errors++;
    }
    ok (errors == 0,
        "hwm.3: flux_reduce_append added 20 items");
    cmp_ok (reduce_calls, "==", 9,
        "hwm.3: op.reduce called 9 times");
    cmp_ok (sink_calls, "==", 11,
        "hwm.3: op.sink called 11 times");
    cmp_ok (sink_items, "==", 20,
        "hwm.3: op.sink handled 20 items");
    ok (flux_reduce_append (r, xstrdup ("hi"), 4) == 0,
        "hwm.4: flux_reduce_append added one item");
    hwm = 0;
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_HWM, &hwm, sizeof (hwm)) == 0
        && hwm == 10,
        "hwm.4: hwm is still 10");

    flux_reduce_destroy (r);
}

void test_nopolicy (flux_t *h)
{
    flux_reduce_t *r;
    int i, errors;

    clear_counts ();

    ok ((r = flux_reduce_create (h, reduce_ops, 0., NULL, 0)) != NULL,
        "nopolicy: flux_reduce_create works");

    errors = 0;
    for (i = 0; i < 100; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 0) < 0)
            errors++;
    }
    ok (errors == 0,
        "nopolicy: flux_reduce_append added 100 items in batch 0");
    cmp_ok (forward_calls, "==", 0,
        "nopolicy: op.forward not called as we are rank 0");
    cmp_ok (reduce_calls, "==", 0,
        "nopolicy: op.reduce not called as we have no flush policy");
    cmp_ok (sink_calls, "==", 100,
        "nopolicy: op.sink called 100 times");
    cmp_ok (sink_items, "==", 100,
        "nopolicy: op.sink processed 100 items");

    flux_reduce_destroy (r);
}

void test_timed (flux_t *h)
{
    flux_reduce_t *r;
    int i, errors;
    double timeout;

    clear_counts ();

    ok ((r = flux_reduce_create (h, reduce_ops, 0.1, NULL,
                                 FLUX_REDUCE_TIMEDFLUSH)) != NULL,
        "timed: flux_reduce_create works");
    if (!r)
        BAIL_OUT();
    ok (flux_reduce_opt_get (r, FLUX_REDUCE_OPT_TIMEOUT, &timeout,
                             sizeof (timeout)) == 0 && timeout == 0.1,
        "timed: flux_reduce_opt_get TIMEOUT returned timeout");

    /* Append 100 items in batch 0 before starting reactor.
     * Reduction occurs at each append.
     * Nothing should be sinked.
     */
    errors = 0;
    for (i = 0; i < 100; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 0) < 0)
            errors++;
    }
    ok (errors == 0,
        "timed.0: flux_reduce_append added 100 items");
    cmp_ok (reduce_calls, "==", 99,
        "timed.0: op.reduce called 99 times");
    cmp_ok (sink_calls, "==", 0,
        "timed.0: op.sink called 0 times");

    /* Start reactor so timeout handler can run.
     * It should fire once and sink all items in one sink call.
     */
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "timed.0: reactor completed normally");
    cmp_ok (sink_calls, "==", 1,
        "timed.0: op.sink called 1 time");
    cmp_ok (sink_items, "==", 100,
        "timed.0: op.sink processed 100 items");

    clear_counts ();

    /* Now append one more item to batch 0.
     * It should be immediately flushed.
     */
    ok (flux_reduce_append (r, xstrdup ("hi"), 0) == 0,
        "timed.0: flux_reduce_append added 1 more item");
    cmp_ok (reduce_calls, "==", 0,
        "timed.0: op.reduce not called");
    cmp_ok (sink_calls, "==", 1,
        "timed.0: op.sink called 1 time");
    cmp_ok (sink_items, "==", 1,
        "timed.0: op.sink processed 1 items");

    clear_counts ();

    /* Append 100 items to batch 1.
     * It should behave like the first batch.
     */
    errors = 0;
    for (i = 0; i < 100; i++) {
        if (flux_reduce_append (r, xstrdup ("hi"), 1) < 0)
            errors++;
    }
    ok (errors == 0,
        "timed.1: flux_reduce_append added 100 items");
    cmp_ok (reduce_calls, "==", 99,
        "timed.1: op.reduce called 99 times");
    cmp_ok (sink_calls, "==", 0,
        "timed.1: op.sink called 0 times");

    /* Start reactor so timeout handler can run.
     * It should fire once and sink all items in one sink call.
     */
    ok (flux_reactor_run (flux_get_reactor (h), 0) == 0,
        "timed.1: reactor completed normally");
    cmp_ok (sink_calls, "==", 1,
        "timed.1: op.sink called 1 time");
    cmp_ok (sink_items, "==", 100,
        "timed.1: op.sink processed 100 items");

    flux_reduce_destroy (r);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (1+6+37+18);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");

    flux_attr_fake (h, "rank", "0", FLUX_ATTRFLAG_IMMUTABLE);
    flux_attr_fake (h, "tbon.level", "0", FLUX_ATTRFLAG_IMMUTABLE);
    flux_attr_fake (h, "tbon.maxlevel", "0", FLUX_ATTRFLAG_IMMUTABLE);

    test_nopolicy (h); // 6
    test_hwm (h); // 37
    test_timed(h); // 18

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

