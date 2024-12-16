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

#include <locale.h>

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

struct jobkey_input {
    bool guest;
    bool namespace_set;
    flux_jobid_t id;
    const char *key;
    const char *expected;
};

struct jobkey_input jobkeytab[] = {
    { false, false, 1, NULL,           "job.0000.0000.0000.0001" },
    { false, false, 2, "foo",          "job.0000.0000.0000.0002.foo" },
    { false, false, 3, "a.b.c",        "job.0000.0000.0000.0003.a.b.c" },
    { false, false, 3, "a.b.c.",        "job.0000.0000.0000.0003.a.b.c." },
    { false, false, 0xdeadbeef, NULL,  "job.0000.0000.dead.beef" },

    /* expected failure: overflow */
    { false, false, 4, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", NULL },

    /* guest (FLUX_KVS_NAMESPACE unset) */
    { true, false, 1, NULL,           "job.0000.0000.0000.0001.guest" },
    { true, false, 2, "foo",          "job.0000.0000.0000.0002.guest.foo" },
    { true, false, 3, "a.b.c",        "job.0000.0000.0000.0003.guest.a.b.c" },
    { true, false, 3, "a.b.c.",        "job.0000.0000.0000.0003.guest.a.b.c." },

    /* guest (FLUX_KVS_NAMESPACE set) */
    { true, true, 1, NULL,           "." },
    { true, true, 2, "foo",          "foo" },
    { true, true, 3, "a.b.c",        "a.b.c" },
    { true, true, 3, "a.b.c.",        "a.b.c." },

    { false, false, 0, NULL, NULL },
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
    if (try->namespace_set)
        setenv ("FLUX_KVS_NAMESPACE", "foo", 1);
    if (try->guest)
        len = flux_job_kvs_guest_key (path, sizeof (path), try->id, try->key);
    else
        len = flux_job_kvs_key (path, sizeof (path), try->id, try->key);
    unsetenv ("FLUX_KVS_NAMESPACE");

    if (try->expected) {
        if (len >= 0 && len == strlen (try->expected)
                     && streq (path, try->expected))
            valid = true;
    }
    else { // expected failure
        if (len < 0)
            valid = true;
    }
    ok (valid == true,
        "util_jobkey id=%ju key=%s %s",
        (uintmax_t)try->id,
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
    ok (flux_job_list (NULL, 0, "{}", 0, 0) == NULL && errno == EINVAL,
        "flux_job_list h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, -1, "{}", 0, 0) == NULL && errno == EINVAL,
        "flux_job_list max_entries=-1 fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_list json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, "wrong", 0, 0) == NULL && errno == EINVAL,
        "flux_job_list json_str=(inval JSON) fails with EINVAL");

    errno = 0;
    ok (flux_job_list (h, 0, "{}", 0, 0xFF) == NULL && errno == EINVAL,
        "flux_job_list states=(illegal states) fails with EINVAL");

    /* flux_job_list_inactive */

    errno = 0;
    ok (flux_job_list_inactive (NULL, 0, 0., "{}") == NULL && errno == EINVAL,
        "flux_job_list_inactive h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list_inactive (h, -1, 0., "{}") == NULL && errno == EINVAL,
        "flux_job_list_inactive max_entries < 0 fails with EINVAL");

    errno = 0;
    ok (flux_job_list_inactive (h, 0, -1, "{}") == NULL && errno == EINVAL,
        "flux_job_list_inactive timestamp < 0 fails with EINVAL");

    errno = 0;
    ok (flux_job_list_inactive (h, 0, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_list_inactive json_str=NULL fails with EINVAL");

    /* flux_job_list_id */

    errno = 0;
    ok (flux_job_list_id (NULL, 0, "{}") == NULL
        && errno == EINVAL,
        "flux_job_list_id h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list_id (h, 0, NULL) == NULL
        && errno == EINVAL,
        "flux_job_list_id json_str=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_list_id (h, 0, "wrong") == NULL
        && errno == EINVAL,
        "flux_job_list_id json_str=(inval JSON) fails with EINVAL");

    /* flux_job_raise */

    errno = 0;
    ok (flux_job_raise (NULL, 0, "cancel", 0, NULL) == NULL && errno == EINVAL,
        "flux_job_raise h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_job_raise (h, 0, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_job_raise type=NULL fails with EINVAL");

    /* flux_job_set_urgency */

    errno = 0;
    ok (flux_job_set_urgency (NULL, 0, 0) == NULL && errno == EINVAL,
        "flux_job_set_urgency h=NULL fails with EINVAL");

    /* flux_job_kvs_key */

    errno = 0;
    ok (flux_job_kvs_key (NULL, 0, 0, NULL) < 0
        && errno == EINVAL,
        "flux_job_kvs_key fails with errno == EINVAL");

    /* flux_job_eventlog_watch */

    errno = 0;
    ok (!flux_job_event_watch (NULL, 0, NULL, 0)
        && errno == EINVAL,
        "flux_job_event_watch fails with EINVAL on bad input");

    errno = 0;
    ok (flux_job_event_watch_get (NULL, NULL) < 0
        && errno == EINVAL,
        "flux_job_event_watch_get fails with EINVAL on bad input");

    errno = 0;
    ok (flux_job_event_watch_cancel (NULL) < 0
        && errno == EINVAL,
        "flux_job_event_watch_cancel fails with EINVAL on bad input");

    /* flux_job_wait */
    errno = 0;
    ok (flux_job_wait (NULL, 0) == NULL && errno == EINVAL,
        "flux_job_wait h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_wait_get_status (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_wait_get_status f=NULL fails with EINVAL");

    errno = 0;
    ok (flux_job_wait_get_id (NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_wait_get_id f=NULL fails with EINVAL");
}

struct ss {
    flux_job_state_t state;
    const char *s;
    const char *s_long;
    const char *s_lower;
    const char *s_long_lower;
};

struct ss sstab[] = {
    { FLUX_JOB_STATE_NEW,      "N", "NEW", "n", "new" },
    { FLUX_JOB_STATE_DEPEND,   "D", "DEPEND", "d", "depend" },
    { FLUX_JOB_STATE_PRIORITY, "P", "PRIORITY", "p", "priority" },
    { FLUX_JOB_STATE_SCHED,    "S", "SCHED", "s", "sched" },
    { FLUX_JOB_STATE_RUN,      "R", "RUN", "r", "run" },
    { FLUX_JOB_STATE_CLEANUP,  "C", "CLEANUP", "c", "cleanup" },
    { FLUX_JOB_STATE_INACTIVE, "I", "INACTIVE", "i", "inactive" },
    { FLUX_JOB_STATE_PENDING,  "PD", "PENDING", "pd", "pending" },
    { FLUX_JOB_STATE_RUNNING,  "RU", "RUNNING", "ru", "running" },
    { FLUX_JOB_STATE_ACTIVE,   "A",  "ACTIVE",  "a",  "active" },
    { -1, NULL, NULL, NULL, NULL },
};

void check_statestr(void)
{
    struct ss *ss;

    for (ss = &sstab[0]; ss->s != NULL; ss++) {
        const char *s = flux_job_statetostr (ss->state, "S");
        const char *s_long = flux_job_statetostr (ss->state, "L");
        const char *s_lower = flux_job_statetostr (ss->state, "s");
        const char *s_long_lower = flux_job_statetostr (ss->state, "l");
        ok (s && streq (s, ss->s),
            "flux_job_statetostr (%d, S) = %s", ss->state, ss->s);
        ok (s_long && streq (s_long, ss->s_long),
            "flux_job_statetostr (%d, L) = %s", ss->state, ss->s_long);
        ok (s_lower && streq (s_lower, ss->s_lower),
            "flux_job_statetostr (%d, s) = %s", ss->state, ss->s_lower);
        ok (s_long_lower && streq (s_long_lower, ss->s_long_lower),
            "flux_job_statetostr (%d, l) = %s", ss->state, ss->s_long_lower);
    }
    for (ss = &sstab[0]; ss->s != NULL; ss++) {
        flux_job_state_t state;
        ok (flux_job_strtostate (ss->s, &state) == 0
            && state == ss->state,
            "flux_job_strtostate (%s) = %d", ss->s, ss->state);
        ok (flux_job_strtostate (ss->s_long, &state) == 0
            && state == ss->state,
            "flux_job_strtostate (%s) = %d", ss->s_long, ss->state);
        ok (flux_job_strtostate (ss->s_lower, &state) == 0
            && state == ss->state,
            "flux_job_strtostate (%s) = %d", ss->s_lower, ss->state);
        ok (flux_job_strtostate (ss->s_long_lower, &state) == 0
            && state == ss->state,
            "flux_job_strtostate (%s) = %d", ss->s_long_lower, ss->state);
    }
    ok (flux_job_statetostr (0, "S") != NULL,
        "flux_job_statetostr (0, S) returned non-NULL");
    ok (flux_job_statetostr (0, "s") != NULL,
        "flux_job_statetostr (0, s) returned non-NULL");
    ok (flux_job_statetostr (0, "L") != NULL,
        "flux_job_statetostr (0, L) returned non-NULL");
    ok (flux_job_statetostr (0, "l") != NULL,
        "flux_job_statetostr (0, l) returned non-NULL");
    ok (flux_job_statetostr (0, "") != NULL,
        "flux_job_statetostr (0, <empty string>) returned non-NULL");
    ok (flux_job_statetostr (0, NULL) != NULL,
        "flux_job_statetostr (0, NULL) returned non-NULL");
}

struct rr {
    flux_job_result_t result;
    const char *r;
    const char *r_long;
    const char *r_lower;
    const char *r_long_lower;
};

struct rr rrtab[] = {
    { FLUX_JOB_RESULT_COMPLETED, "CD", "COMPLETED", "cd", "completed" },
    { FLUX_JOB_RESULT_FAILED,    "F",  "FAILED", "f", "failed" },
    { FLUX_JOB_RESULT_CANCELED,  "CA", "CANCELED", "ca", "canceled" },
    { FLUX_JOB_RESULT_TIMEOUT,   "TO", "TIMEOUT", "to", "timeout" },
    { -1, NULL, NULL, NULL, NULL },
};

void check_resultstr(void)
{
    struct rr *rr;

    for (rr = &rrtab[0]; rr->r != NULL; rr++) {
        const char *r = flux_job_resulttostr (rr->result, "S");
        const char *r_long = flux_job_resulttostr (rr->result, "L");
        const char *r_lower = flux_job_resulttostr (rr->result, "s");
        const char *r_long_lower = flux_job_resulttostr (rr->result, "l");
        ok (r && streq (r, rr->r),
            "flux_job_resulttostr (%d, S) = %s", rr->result, rr->r);
        ok (r_long && streq (r_long, rr->r_long),
            "flux_job_resulttostr (%d, L) = %s", rr->result, rr->r_long);
        ok (r_lower && streq (r_lower, rr->r_lower),
            "flux_job_resulttostr (%d, s) = %s", rr->result, rr->r_lower);
        ok (r_long_lower && streq (r_long_lower, rr->r_long_lower),
            "flux_job_resulttostr (%d, l) = %s", rr->result, rr->r_long_lower);
    }
    for (rr = &rrtab[0]; rr->r != NULL; rr++) {
        flux_job_result_t result;
        ok (flux_job_strtoresult (rr->r, &result) == 0 && result == rr->result,
            "flux_job_strtoresult (%s) = %d", rr->r, rr->result);
        ok (flux_job_strtoresult (rr->r_long, &result) == 0 && result == rr->result,
            "flux_job_strtoresult (%s) = %d", rr->r_long, rr->result);
    }
    ok (flux_job_resulttostr (0, "S") != NULL,
        "flux_job_resulttostr (0, S) returned non-NULL");
    ok (flux_job_resulttostr (0, "L") != NULL,
        "flux_job_resulttostr (0, L) returned non-NULL");
    ok (flux_job_resulttostr (0, "") != NULL,
        "flux_job_resulttostr (0, <empty string>) returned non-NULL");
    ok (flux_job_resulttostr (0, NULL) != NULL,
        "flux_job_resulttostr (0, NULL) returned non-NULL");
}

void check_kvs_namespace (void)
{
    char buf[64];
    ok (flux_job_kvs_namespace (buf, 64, 1234) == strlen ("job-1234"),
        "flux_job_kvs_namespace works");
    is (buf, "job-1234",
        "flux_job_kvs_namespace returns expected namespace name");
    ok (flux_job_kvs_namespace (buf, 7, 1234) < 0 && errno == EOVERFLOW,
        "flux_job_kvs_namespace returns EOVERFLOW for too small buffer");
    ok (flux_job_kvs_namespace (buf, -1, 1234) < 0 && errno == EINVAL,
        "flux_job_kvs_namespace returns EINVAL on invalid buffer size");
    ok (flux_job_kvs_namespace (NULL, 64, 1234) < 0 && errno == EINVAL,
        "flux_job_kvs_namespace returns EINVAL on invalid buffer");
}

struct jobid_parse_test {
    const char *type;
    flux_jobid_t id;
    const char *string;
};

struct jobid_parse_test jobid_parse_tests[] = {
    { "dec",    0,     "0" },
    { "hex",    0,     "0x0" },
    { "dothex", 0,     "0000.0000.0000.0000" },
    { "kvs",    0,     "job.0000.0000.0000.0000" },
    { "words",  0,     "academy-academy-academy--academy-academy-academy" },
    { "emoji",  0,     "😃" },
#if ASSUME_BROKEN_LOCALE
    { "f58",    0,     "f1" },
#else
    { "f58",    0,     "ƒ1" },
#endif

    { "dec",    1,     "1" },
    { "hex",    1,     "0x1" },
    { "dothex", 1,     "0000.0000.0000.0001" },
    { "kvs",    1,     "job.0000.0000.0000.0001" },
    { "words",  1,     "acrobat-academy-academy--academy-academy-academy" },
    { "emoji",  1,     "😄" },
#if ASSUME_BROKEN_LOCALE
    { "f58",    1,     "f2" },
#else
    { "f58",    1,     "ƒ2" },
#endif

    { "dec",    65535, "65535" },
    { "hex",    65535, "0xffff" },
    { "dothex", 65535, "0000.0000.0000.ffff" },
    { "kvs",    65535, "job.0000.0000.0000.ffff" },
    { "words",  65535, "nevada-archive-academy--academy-academy-academy" },
    { "emoji",  65535, "💁📚" },
#if ASSUME_BROKEN_LOCALE
    { "f58",    65535, "fLUv" },
#else
    { "f58",    65535, "ƒLUv" },
#endif

    { "dec",    6787342413402046, "6787342413402046" },
    { "hex",    6787342413402046, "0x181d0d4d850fbe" },
    { "dothex", 6787342413402046, "0018.1d0d.4d85.0fbe" },
    { "kvs",    6787342413402046, "job.0018.1d0d.4d85.0fbe" },
    { "words",  6787342413402046, "cake-plume-nepal--neuron-pencil-academy" },
    { "emoji",  6787342413402046, "👴😱🔚🎮🕙🚩" },
#if ASSUME_BROKEN_LOCALE
    { "f58",    6787342413402046, "fuzzybunny" },
#else
    { "f58",    6787342413402046, "ƒuzzybunny" },
#endif

    { NULL, 0, NULL }
};

void check_jobid_parse_encode (void)
{
    char buf[1024];
    flux_jobid_t jobid;
    struct jobid_parse_test *tp = jobid_parse_tests;
    while (tp->type != NULL) {
        memset (buf, 0, sizeof (buf));
        if (MB_CUR_MAX == 1 && streq (tp->type, "f58")) {
            tap_skip (4, "Skipping F58 encode/decode due to current locale");
            tp++;
            continue;
        }
        ok (flux_job_id_encode (tp->id, tp->type, buf, sizeof (buf)) == 0,
            "flux_job_id_encode (%ju, %s) == 0", (uintmax_t) tp->id, tp->type);
        is (buf, tp->string,
            "flux_job_id_encode() got %s", buf);
        ok (flux_job_id_parse (buf, &jobid) == 0,
            "flux_job_id_parse() of result works: %s", strerror (errno));
        ok (jobid == tp->id,
            "flux_job_id_parse() returned correct id");
        tp++;
    }

    ok (flux_job_id_encode (1234, NULL, buf, sizeof (buf)) == 0,
        "flux_job_id_encode() with NULL type works");
    is (buf, "1234",
        "flux_job_id_encode() encodes to decimal by default");

    ok (flux_job_id_parse ("  1234  ", &jobid) == 0,
        "flux_job_id_parse works with leading whitespace");
    ok (jobid == 1234,
        "flux_job_id_parse got expected result");

    ok (flux_job_id_encode (1234, NULL, NULL, 33) < 0 && errno == EINVAL,
        "flux_job_id_encode with NULL buffer returns EINVAL");
    ok (flux_job_id_encode (1234, "dec", buf, 4) < 0 && errno == EOVERFLOW,
        "flux_job_id_encode with too small buffer returns EOVERFLOW");
    ok (flux_job_id_encode (1234, "dothex", buf, 19) < 0 && errno == EOVERFLOW,
        "flux_job_id_encode with too small buffer returns EOVERFLOW");
    ok (flux_job_id_encode (1234, "foo", buf, 1024) < 0 && errno == EPROTO,
        "flux_job_id_encode with unknown encode type returns EPROTO");
}

static void check_job_timeleft (void)
{
    flux_t *h = (flux_t *)(uintptr_t)42; // fake but non-NULL
    flux_error_t error;
    double timeleft;

    ok (flux_job_timeleft (NULL, &error, &timeleft) < 0 && errno == EINVAL,
        "flux_job_timeleft (NULL, ...) returns EINVAL");
    ok (flux_job_timeleft (h, &error, NULL) < 0 && errno == EINVAL,
        "flux_job_timeleft (h, error, NULL) returns EINVAL");
}

static void check_waitstatus_to_exitcode (void)
{
    flux_error_t error;
    ok (flux_job_waitstatus_to_exitcode (-1, &error) < 0 && errno == EINVAL,
        "flux_job_waitstatus_to_exitcode (-1) returns EINVAL");
    is (error.text, "unexpected wait(2) status -1",
        "error.text is %s", error.text);
    ok (flux_job_waitstatus_to_exitcode (0, &error) == 0,
        "flux_job_waitstatus_to_exitcode (0) returns 0");
    is (error.text, "",
        "error.text is cleared");
    ok (flux_job_waitstatus_to_exitcode (9, &error) == 128+9,
        "flux_job_waitstatus_to_exitcode (9) == %d", 128+9);
    like (error.text, "job shell Killed",
        "error.text is %s", error.text);
    ok (flux_job_waitstatus_to_exitcode (1<<8, &error) == 1,
        "flux_job_waitstatus_to_exitcode (1<<8) = 1");
    is (error.text, "task(s) exited with exit code 1",
        "error.text is %s", error.text);
    ok (flux_job_waitstatus_to_exitcode ((128+15)<< 8, &error) == 128+15,
        "flux_job_waitstatus_to_exitcode ((128+15)<<8) = 128+15");
    like (error.text, "task\\(s\\) Terminated",
        "error.text is %s", error.text);
    ok (flux_job_waitstatus_to_exitcode ((128+11)<<8, &error) == 128+11,
        "flux_job_waitstatus_to_exitcode ((128+11)<<8) = 128+11");
    like (error.text, "task\\(s\\) Segmentation fault",
        "error.text is %s", error.text);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    /* fluid F58 tests require unicode locale initialization */
    setlocale (LC_ALL, "en_US.UTF-8");
    unsetenv ("FLUX_F58_FORCE_ASCII");

    check_jobkey ();

    check_corner_case ();

    check_statestr ();

    check_resultstr ();

    check_kvs_namespace ();

    check_jobid_parse_encode ();

    check_job_timeleft ();

    check_waitstatus_to_exitcode ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
