#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <alloca.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/coproc.h"
#include "src/common/libutil/xzmalloc.h"

static bool death = false;

coproc_t *co;

/* Handler will yield if it segfaulted; return if not.
 */
void sigsegv_handler (int sig)
{
    coproc_yield (co);
}

/* Handling SIGSEGV is tricky.
 * If we've blown the stack, we need to call the handler in its own stack.
 */
int signal_setup (void)
{
    struct sigaction act;
    stack_t ss;

    ss.ss_sp = xzmalloc (SIGSTKSZ);
    ss.ss_flags = 0;
    ss.ss_size = SIGSTKSZ;
    if (sigaltstack (&ss, NULL) < 0)
        return -1;

    memset (&act, 0, sizeof (act));
    sigemptyset (&act.sa_mask);
    act.sa_handler = sigsegv_handler;
    act.sa_flags = SA_ONSTACK;

    return sigaction (SIGSEGV, &act, NULL);
}

/* Touch the stack.  If we touch guard page, we should get a SIGSEGV.
 */
int stack_cb (coproc_t *c, void *arg)
{
    size_t *ssize = arg;
    void *ptr = alloca (*ssize);
    diag ("alloca %d = %p", *ssize, ptr);
    if (ptr)
        memset (ptr, 0x66, *ssize);
    return ptr ? 0 : -1;
}

int bar_cb (coproc_t *c, void *arg)
{
    while (!death) {
        if (coproc_yield (c) < 0)
            return -1;
    }
    return 0;
}

int foo_cb (coproc_t *c, void *arg)
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
    coproc_t *c;

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
    coproc_t *c;
    int i;
    int rc;

    plan (29);

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

    coproc_t *cps[10000];
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

    /* Test stack guard page(s)
     */
    ok (signal_setup () == 0,
        "installed SIGSEGV handler with sigaltstack");
    ok ((co = coproc_create (stack_cb)) != NULL,
        "coproc_create works");
    size_t ssize = coproc_get_stacksize (co);
    ok (ssize > 0,
        "coproc_get_stacksize returned %d", ssize);

    /* We can't use all of the stack and get away with it.
     * FIXME: it is unclear why this number must be so large.
     * I found it experimentally; maybe it is non-portable and that
     * will make this test fragile?
     */
    //const size_t stack_reserve = 2560; // XXX 2540 is too small
    const size_t stack_reserve = 3000;

    /* should be OK */
    ssize -= stack_reserve;
    ok (coproc_start (co, &ssize) == 0,
        "coproc_start works");
    rc = -1;
    ok (coproc_returned (co, &rc) && rc == 0,
        "coproc successfully scribbled on stack");

    /* should fail */
    ssize += stack_reserve + 8;
    ok (coproc_start (co, &ssize) == 0,
        "coproc_start works");
    ok (!coproc_returned (co, NULL),
        "coproc scribbled on guard page and segfaulted");

    done_testing ();

}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
