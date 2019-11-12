/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>

#include "src/common/libtap/tap.h"

#include "src/modules/job-manager/job.h"
#include "src/modules/job-manager/list.h"

int main (int argc, char *argv[])
{
    json_t *jobs;
    json_t *el;
    json_t *id_o;
    struct job *job;
    flux_jobid_t id;

    plan (NO_PLAN);

    if (!(jobs = json_array ()))
        BAIL_OUT ("json_array() failed");
    if (!(job = job_create ()))
        BAIL_OUT ("job_create() failed");
    job->id = 1;

    ok (list_append_job (jobs, job) == 0,
        "list_append_job works");
    ok (json_array_size (jobs) == 1,
         "array has expected size");

    el = json_array_get (jobs, 0);
    ok (el != NULL && json_is_object (el),
        "array[0] is an object");
    ok ((id_o = json_object_get (el, "id")) != NULL,
        "array[0] id is set");
    id = json_integer_value (id_o);
    ok (id == 1,
        "array[0] id=1");
    if (id != 1)
        diag ("id=%d", (int)id);
    ok (json_object_size (el) == 5,
        "array[1] size=5");

    json_decref (jobs);
    job_decref (job);

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
