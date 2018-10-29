#include <jansson.h>

#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/queue.h"
#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/list.h"

/* Create queue of size jobs
 *   id: [0:size-1]
 */
struct queue *make_test_queue (int size)
{
    struct queue *q;
    flux_jobid_t id;

    q = queue_create ();
    if (!q)
        BAIL_OUT ("could not create queue");

    for (id = 0; id < size; id++) {
        struct job *j;
        if (!(j = job_create (id, 0, 0, 0, 0)))
            BAIL_OUT ("job_create failed");
        if (queue_insert (q, j) < 0)
            BAIL_OUT ("queue_insert failed");
    }
    return q;
}

int main (int argc, char *argv[])
{
    const int q_size = 16;
    struct queue *q;
    struct job *j;
    json_t *attrs;
    json_t *badattrs;
    json_t *o;
    json_t *el;
    json_t *id_o;
    flux_jobid_t id;

    plan (NO_PLAN);

    q = make_test_queue (q_size);
    if (!(attrs = json_pack ("[s]", "id")))
        BAIL_OUT ("json_pack failed");

    /* list_job_array */

    o = list_job_array (q, 0, attrs);
    ok (o != NULL && json_is_array (o),
        "list_job_array returns array");
    ok (json_array_size (o) == q_size,
        "array has expected size");
    json_decref (o);

    o = list_job_array (q, 4, attrs);
    ok (o != NULL && json_is_array (o),
        "list_job_array max_entries=4 returns array");
    ok (json_array_size (o) == 4,
        "array has expected size");
    el = json_array_get (o, 1);
    ok (el != NULL && json_is_object (el),
        "array[1] is an object");
    ok ((id_o = json_object_get (el, "id")) != NULL,
        "array[1] id is set");
    id = json_integer_value (id_o);
    ok (id == 1,
        "array[1] id=1");
    if (id != 1)
        diag ("id=%d", (int)id);
    ok (json_object_size (el) == 1,
        "array[1] size=1");
    json_decref (o);

    errno = 0;
    ok (list_job_array (q, -1, attrs) == NULL && errno == EPROTO,
        "list_job_array max_entries < 0 fails with EPROTO");

    errno = 0;
    ok (list_job_array (q, -1, NULL) == NULL && errno == EPROTO,
        "list_job_array attrs=NULL fails with EPROTO");

    /* list_one_job */

    if (!(j = queue_lookup_by_id (q, 0)))
        BAIL_OUT ("queue_lookup_by_id 0 failed");
    if (!(badattrs = json_pack ("[s]", "foo")))
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (list_one_job (j, badattrs) == NULL && errno == EINVAL,
        "list_one_job attrs=[\"foo\"] fails with EINVAL");
    json_decref (badattrs);

    if (!(badattrs = json_pack ("[i]", 42)))
        BAIL_OUT ("json_pack failed");
    errno = 0;
    ok (list_one_job (j, badattrs) == NULL && errno == EPROTO,
        "list_one_job attrs=[42] fails with EPROTO");
    json_decref (badattrs);


    json_decref (attrs);
    queue_destroy (q);

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
