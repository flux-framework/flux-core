#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>

#include "src/common/libtap/tap.h"
#include "src/modules/wreck/wreck_job.h"

static int free_fun_count = 0;
void free_fun (void *arg)
{
    free_fun_count++;
}

void basic (void)
{
    struct wreck_job *job;
    const char *s;

    job = wreck_job_create ();
    ok (job != NULL,
        "wreck_job_create works");
    wreck_job_set_state (job, "submitted");
    s = wreck_job_get_state (job);
    ok (s != NULL && !strcmp (s, "submitted"),
        "wreck_job_get/set_state works");

    wreck_job_set_aux (job, job, free_fun);
    ok (wreck_job_get_aux (job) == job,
        "wreck_job_get/set_aux works");

    wreck_job_set_aux (job, job, free_fun);
    ok (free_fun_count == 1,
        "wreck_job_set_aux calls destructor when aux overwritten");
    wreck_job_destroy (job);
    ok (free_fun_count == 2,
        "wreck_job_destry calls aux destructor");
}

void hash (void)
{
    zhash_t *h = zhash_new ();
    struct wreck_job *job;

    if (!h)
        BAIL_OUT ("zhash_new failed");

    job = wreck_job_create ();
    if (!job)
        BAIL_OUT ("wreck_job_create failed");
    job->id = 42;
    ok (wreck_job_insert (job, h) == 0 && zhash_size (h) == 1,
        "wreck_job_insert 42 works");

    job = wreck_job_create ();
    if (!job)
        BAIL_OUT ("wreck_job_create failed");
    job->id = 43;
    ok (wreck_job_insert (job, h) == 0 && zhash_size (h) == 2,
        "wreck_job_insert 43 works");

    job = wreck_job_lookup (42, h);
    ok (job != NULL && job->id == 42,
        "wreck_job_lookup 42 works");
    wreck_job_set_aux (job, job, free_fun);

    job = wreck_job_lookup (43, h);
    ok (job != NULL && job->id == 43,
        "wreck_job_lookup 43 works");
    wreck_job_set_aux (job, job, free_fun);

    errno = 0;
    job = wreck_job_lookup (2, h);
    ok (job == NULL && errno == ENOENT,
        "wreck_job_lookup 2 fails with ENOENT");

    free_fun_count = 0;
    zhash_destroy (&h);
    ok (free_fun_count == 2,
        "zhash_destroy frees jobs");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    hash ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
