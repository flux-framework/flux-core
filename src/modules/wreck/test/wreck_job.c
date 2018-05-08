#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>

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
    wreck_job_incref (job);
    ok (job->refcount == 2,
        "wreck_job_incref increases refcount");
    wreck_job_destroy (job);
    ok (job->refcount== 1,
        "wreck_job_destroy decreases refcount");
    ok (free_fun_count == 1,
        "wreck_job_destroy doesn't call aux destructor until refcount == 0");

    wreck_job_destroy (job);
    ok (free_fun_count == 2,
        "wreck_job_destroy calls aux destructor");
}

static int count_entries (const char *json_str)
{
    json_t *obj = NULL;
    json_t *array = NULL;
    int n = 0;

    if (!json_str || !(obj = json_loads (json_str, 0, NULL))
                  || json_unpack (obj, "{s:o}", "jobs", &array) < 0)
        BAIL_OUT ("JSON parse error");
    n = json_array_size (array);
    json_decref (obj);
    return n;
}

void hash (void)
{
    zhash_t *h = zhash_new ();
    struct wreck_job *job;
    char *s = NULL;
    json_t *obj = NULL;
    json_t *array = NULL;

    if (!h)
        BAIL_OUT ("zhash_new failed");

    job = wreck_job_create ();
    if (!job)
        BAIL_OUT ("wreck_job_create failed");
    job->id = 42;
    wreck_job_set_state (job, "submitted");
    ok (wreck_job_insert (job, h) == 0 && zhash_size (h) == 1,
        "wreck_job_insert 42 works");

    job = wreck_job_create ();
    if (!job)
        BAIL_OUT ("wreck_job_create failed");
    job->id = 43;
    wreck_job_set_state (job, "complete");
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

    /* List has two entries, one "complete", one "submitted".
     */
    s = wreck_job_list (h, 0, NULL, NULL);
    ok (s != NULL && (obj = json_loads (s, 0, NULL)) && json_is_object (obj),
        "wreck_job_list produces a JSON object");
    diag ("%s", s ? s : "(null)");
    ok (json_unpack (obj, "{s:o}", "jobs", &array) == 0,
        "JSON object has one member");
    ok (json_is_array (array) && json_array_size (array) == 2,
        "member is array with expected size");
    json_decref (obj);
    free (s);

    /* 'max' can limit number of entries returned.
     */
    s = wreck_job_list (h, 1, NULL, NULL);
    ok (count_entries (s) == 1,
        "wreck_job_list max=1 limits entries to 1");
    free (s);

    /* If 'include' and no match (and no 'exclude'), zero entries returned .
     */
    s = wreck_job_list (h, 0, "badstate", NULL);
    ok (count_entries (s) == 0,
        "wreck_job_list include=badstate returns 0 entries");
    free (s);

    /* If 'exclude' and no match (and no 'include'), all entries returned
     */
    s = wreck_job_list (h, 0, NULL, "badstate");
    ok (count_entries (s) == 2,
        "wreck_job_list exclude=badstate returns 2 entries");
    free (s);

    /* If 'include' and one match (and no 'exclude'), 1 of 2 entries returned
     */
    s = wreck_job_list (h, 0, "complete", NULL);
    ok (count_entries (s) == 1,
        "wreck_job_list include=complete returns 1 entry");
    free (s);

    /* If 'exclude' and one match (and no 'include'), 1 of 2 entries returned
     */
    s = wreck_job_list (h, 0, NULL, "complete");
    ok (count_entries (s) == 1,
        "wreck_job_list exclude=complete returns 1 entry");
    free (s);

    /* If 'include' and all match (and no 'exclude'), all entries returned
     */
    s = wreck_job_list (h, 0, "complete,submitted", NULL);
    ok (count_entries (s) == 2,
        "wreck_job_list include=complete,submitted returns 2 entries");
    free (s);

    /* If 'exclude' and all match (and no 'include'), zero entries returned
     */
    s = wreck_job_list (h, 0, NULL, "complete,submitted");
    ok (count_entries (s) == 0,
        "wreck_job_list exclude=complete,submitted returns 0 entries");
    free (s);

    free_fun_count = 0;
    zhash_destroy (&h);
    ok (free_fun_count == 2,
        "zhash_destroy frees jobs");
}

void corner (void)
{
    zhash_t *hash;
    struct wreck_job *job;
    const char *s;

    if (!(hash = zhash_new()))
        BAIL_OUT ("zhash_new failed");
    if (!(job = wreck_job_create ()))
        BAIL_OUT ("wreck_job_create failed)");

    lives_ok ({wreck_job_destroy (NULL);},
        "wreck_job_destroy (job=NULL) doesn't crash");

    lives_ok ({wreck_job_set_state (NULL, "completed");},
        "wreck_job_set_state (job=NULL doesn't crash");
    lives_ok ({wreck_job_set_state (job, NULL);},
        "wreck_job_set_state (state=NULL) doesn't crash");
    lives_ok ({wreck_job_set_state (job, "0123456789abcdef");},
        "wreck_job_set_state (state=too long) doesn't crash");

    s = wreck_job_get_state (NULL);
    ok (s != NULL && strlen (s) == 0,
        "wreck_job_get_state (job=NULL) returns empty string");

    errno = 0;
    ok (wreck_job_insert (NULL, hash) < 0 && errno == EINVAL,
        "wreck_job_insert (job=NULL) fails with EINVAL");
    errno = 0;
    job->id = 42;
    ok (wreck_job_insert (job, NULL) < 0 && errno == EINVAL,
        "wreck_job_insert (hash=NULL) fails with EINVAL");

    errno = 0;
    ok (wreck_job_lookup (-1, hash) == NULL && errno == EINVAL,
        "wreck_job_lookup (id=-1) fails with EINVAL");
    errno = 0;
    ok (wreck_job_lookup (0, hash) == NULL && errno == EINVAL,
        "wreck_job_lookup (id=0) fails with EINVAL");

    errno = 0;
    ok (wreck_job_lookup (1, NULL) == NULL && errno == EINVAL,
        "wreck_job_lookup (hash=NULL) fails with EINVAL");

    lives_ok ({wreck_job_delete (1, NULL);},
        "wreck_job_delete (job=NULL) doesn't crash");
    lives_ok ({wreck_job_set_aux (NULL, NULL, NULL);},
        "wreck_job_set_aux (job=NULL) doesn't crash");
    lives_ok ({wreck_job_get_aux (NULL);},
        "wreck_job_get_aux (job=NULL) doesn't crash");

    errno = 0;
    ok (wreck_job_list (NULL, 0, NULL, NULL) == NULL && errno == EINVAL,
        "wreck_job_list (hash=NULL) fails with EINVAL");

    zhash_destroy (&hash);
    wreck_job_destroy (job);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    hash ();
    corner ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
