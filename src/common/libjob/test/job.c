/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/job.h"

struct jobkey_input {
    flux_jobid_t id;
    bool active;
    const char *key;
    const char *expected;
};

struct jobkey_input jobkeytab[] = {
    { 1, true, NULL,            "job.active.0000.0000.0000.0001" },
    { 1, false, NULL,           "job.inactive.0000.0000.0000.0001" },
    { 2, true, "foo",           "job.active.0000.0000.0000.0002.foo" },
    { 2, false, "foo",          "job.inactive.0000.0000.0000.0002.foo" },
    { 3, true, "a.b.c",         "job.active.0000.0000.0000.0003.a.b.c" },
    { 0xdeadbeef, true, NULL,   "job.active.0000.0000.dead.beef" },

    /* expected failure: overflow */
    { 4, true, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", NULL },

    { 0, false, NULL, NULL },
};
bool is_jobkeytab_end (struct jobkey_input *try)
{
    if (try->id == 0 && try->active == false && !try->key && !try->expected)
        return true;
    return false;
}

void check_one_jobkey (struct jobkey_input *try)
{
    char path[64];
    int len;
    bool valid = false;

    memset (path, 0, sizeof (path));
    len = flux_job_kvs_key (path, sizeof (path), try->active, try->id, try->key);

    if (try->expected) {
        if (len >= 0 && len == strlen (try->expected)
                     && !strcmp (path, try->expected))
            valid = true;
    }
    else { // expected failure
        if (len < 0)
            valid = true;
    }
    ok (valid == true,
        "util_jobkey id=%llu active=%s key=%s %s",
        (unsigned long long)try->id,
        try->active ? "true" : "false",
        try->key ? try->key : "NULL",
        try->expected ? "works" : "fails");

    if (!valid)
        diag ("jobkey: %s", path);
}

void check_jobkey (void)
{
    int i;

    for (i = 0; !is_jobkeytab_end (&jobkeytab[i]); i++)
        check_one_jobkey (&jobkeytab[i]);
}

void check_corner_case (void)
{
    flux_t *h = (flux_t *)(uintptr_t)42; // fake but non-NULL

    /* flux_job_submit */

    errno = 0;
    ok (flux_job_submit (NULL, NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_submit with NULL args fails with EINVAL");

    errno = 0;
    ok (flux_job_submit_get_id (NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_submit_get_id with NULL args fails with EINVAL");

    /* flux_job_list */

    errno = 0;
    ok (flux_job_list (NULL, 0, "{}") == NULL && errno == EINVAL,
        "flux_job_list h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, -1, "{}") == NULL && errno == EINVAL,
        "flux_job_list max_entries=-1 fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_list json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_list json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, "wrong") == NULL && errno == EINVAL,
        "flux_job_list json_str=(inval JSON) fails with EINVAL");

    /* flux_job_raise */

    errno = 0;
    ok (flux_job_raise (NULL, 0, "cancel", 0, NULL) == NULL && errno == EINVAL,
        "flux_job_raise h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_job_raise (h, 0, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_raise type=NULL fails with EINVAL");

    /* flux_job_set_priority */

    errno = 0;
    ok (flux_job_set_priority (NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_set_priority h=NULL fails with EINVAL");

    /* flux_job_kvs_key */

    errno = 0;
    ok (flux_job_kvs_key (NULL, 0, false, 0, NULL) < 0
        && errno == EINVAL,
        "flux_job_kvs_key fails with errno == EINVAL");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_jobkey ();

    check_corner_case ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
