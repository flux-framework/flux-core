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
#    include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libtap/tap.h"

struct jobkey_input {
    flux_jobid_t id;
    const char *key;
    const char *expected;
};

struct jobkey_input jobkeytab[] = {
    {1, NULL, "job.0000.0000.0000.0001"},
    {2, "foo", "job.0000.0000.0000.0002.foo"},
    {3, "a.b.c", "job.0000.0000.0000.0003.a.b.c"},
    {0xdeadbeef, NULL, "job.0000.0000.dead.beef"},

    /* expected failure: overflow */
    {4,
     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
     "xxxxxxxx"
     "xxxxxxxxxx",
     NULL},

    {0, NULL, NULL},
};
bool is_jobkeytab_end (struct jobkey_input *try)
{
    if (try->id == 0 && !try->key && !try->expected)
        return true;
    return false;
}

void check_one_jobkey (struct jobkey_input *try)
{
    char path[64];
    int len;
    bool valid = false;

    memset (path, 0, sizeof (path));
    len = flux_job_kvs_key (path, sizeof (path), try->id, try->key);

    if (try->expected) {
        if (len >= 0 && len == strlen (try->expected)
            && !strcmp (path, try->expected))
            valid = true;
    } else {  // expected failure
        if (len < 0)
            valid = true;
    }
    ok (valid == true,
        "util_jobkey id=%llu key=%s %s",
        (unsigned long long)try->id,
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
    flux_t *h = (flux_t *)(uintptr_t)42;  // fake but non-NULL

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
    ok (flux_job_kvs_key (NULL, 0, 0, NULL) < 0 && errno == EINVAL,
        "flux_job_kvs_key fails with errno == EINVAL");

    /* flux_job_eventlog_watch */

    errno = 0;
    ok (!flux_job_event_watch (NULL, 0) && errno == EINVAL,
        "flux_job_event_watch fails with EINVAL on bad input");

    errno = 0;
    ok (flux_job_event_watch_get (NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_event_watch_get fails with EINVAL on bad input");

    errno = 0;
    ok (flux_job_event_watch_cancel (NULL) < 0 && errno == EINVAL,
        "flux_job_event_watch_cancel fails with EINVAL on bad input");
}

struct ss {
    flux_job_state_t state;
    const char *s;
    const char *s_long;
};

struct ss sstab[] = {
    {FLUX_JOB_NEW, "N", "NEW"},
    {FLUX_JOB_DEPEND, "D", "DEPEND"},
    {FLUX_JOB_SCHED, "S", "SCHED"},
    {FLUX_JOB_RUN, "R", "RUN"},
    {FLUX_JOB_CLEANUP, "C", "CLEANUP"},
    {FLUX_JOB_INACTIVE, "I", "INACTIVE"},
    {-1, NULL, NULL},
};

void check_statestr (void)
{
    struct ss *ss;

    for (ss = &sstab[0]; ss->s != NULL; ss++) {
        const char *s = flux_job_statetostr (ss->state, true);
        const char *s_long = flux_job_statetostr (ss->state, false);
        ok (s && !strcmp (s, ss->s),
            "flux_job_statetostr (%d, true) = %s",
            ss->state,
            ss->s);
        ok (s_long && !strcmp (s_long, ss->s_long),
            "flux_job_statetostr (%d, false) = %s",
            ss->state,
            ss->s_long);
    }
    for (ss = &sstab[0]; ss->s != NULL; ss++) {
        flux_job_state_t state;
        ok (flux_job_strtostate (ss->s, &state) == 0 && state == ss->state,
            "flux_job_strtostate (%s) = %d",
            ss->s,
            ss->state);
        ok (flux_job_strtostate (ss->s_long, &state) == 0 && state == ss->state,
            "flux_job_strtostate (%s) = %d",
            ss->s_long,
            ss->state);
    }
    ok (flux_job_statetostr (0, true) != NULL,
        "flux_job_statetostr (0, true) returned non-NULL");
    ok (flux_job_statetostr (0, false) != NULL,
        "flux_job_statetostr (0, false) returned non-NULL");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_jobkey ();

    check_corner_case ();

    check_statestr ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
