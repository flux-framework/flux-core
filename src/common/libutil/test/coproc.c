#include <pthread.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/coproc.h"

static bool death = false;

int bar_cb (coproc_t c, void *arg)
{
    while (!death) {
        if (coproc_yield (c) < 0)
            return -1;
    }
    return 0;
}

int foo_cb (coproc_t c, void *arg)
{
    int n; /* number of times to yield */

    ok (arg && *(int *)arg >= 0 && *(int *)arg <= 16,
        "coproc args are valid");
    n = *(int *)arg;

    while (n > 0) {
        if (coproc_yield (c) < 0)
            return -1;
        n--;
    }
    return n;
}

void *threadmain (void *arg)
{
    coproc_t c;

    ok ((c = coproc_create (bar_cb)) != NULL,
        "coproc_create works in a pthread");
    ok (coproc_start (c, NULL) == 0,
        "coproc_start works in a pthread");
    ok (!coproc_returned (c, NULL),
        "coproc_start did not return (yielded)");
    coproc_destroy (c);
    return NULL;
}

int main (int argc, char *argv[])
{
    coproc_t c;
    int i;
    int rc;

    plan (22);

    ok ((c = coproc_create (foo_cb)) != NULL,
        "coproc_create works");

    i = 0;
    rc = -1;
    ok (coproc_start (c, &i) == 0,
        "coproc_start works");
    ok (coproc_returned (c, &rc),
        "coproc returned");
    cmp_ok (rc, "==", 0,
        "rc is set to coproc return value");

    i = 2;
    rc = -1;
    ok (coproc_start (c, &i) == 0,
        "coproc_start works");
    ok (!coproc_returned (c, NULL),
        "coproc did not return (yielded)");

    ok (coproc_resume (c) == 0,
        "coproc_resume works");
    ok (!coproc_returned (c, NULL),
        "coproc did not return (yielded)");

    ok (coproc_resume (c) == 0,
        "coproc_resume works");
    ok (coproc_returned (c, &rc),
        "coproc returned");
    cmp_ok (rc, "==", 0,
        "rc is set to coproc return value");

    errno = 0;
    ok (coproc_resume (c) < 0 && errno == EINVAL,
        "coproc_resume on returned coproc fails with EINVAL");

    coproc_destroy (c);

    pthread_t t;
    ok (pthread_create (&t, NULL, threadmain, NULL) == 0,
        "pthread_create OK");
    ok (pthread_join (t, NULL) == 0,
        "pthread_join OK");

    coproc_t cps[10000];
    for (i = 0; i < 10000; i++) {
        if (!(cps[i] = coproc_create (bar_cb)))
            break;
        if (coproc_start (cps[i], NULL) < 0 || coproc_returned (cps[i], NULL))
            break;
    }
    ok (i == 10000,
        "started 10000 coprocs that yielded");
    for (i = 0; i < 10000; i++) {
        if (coproc_resume (cps[i]) < 0 || coproc_returned (cps[i], NULL))
            break;
    }
    ok (i == 10000,
        "resumed 10000 coprocs that yielded");
    death = true;
    for (i = 0; i < 10000; i++) {
        if (coproc_resume (cps[i]) < 0 || !coproc_returned (cps[i], &rc)
                                       || rc != 0)
            break;
    }
    ok (i == 10000,
        "resumed 10000 coprocs that exited with rc=0");

    for (i = 0; i < 10000; i++)
        coproc_destroy (cps[i]);


    done_testing ();

}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
