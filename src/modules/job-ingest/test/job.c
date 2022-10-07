/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "src/common/libtap/tap.h"
#include "src/common/libjob/sign_none.h"

#include "job.h"


flux_msg_t *pack_request (bool owner, const char *fmt, ...)
{
    va_list ap;
    flux_msg_t *msg;
    int rc;
    struct flux_msg_cred cred = {
        .userid = getuid(),
        .rolemask = owner ? FLUX_ROLE_OWNER : FLUX_ROLE_USER,
    };

    if (!(msg = flux_request_encode ("job-ingest.submit", NULL)))
        BAIL_OUT ("could not create request");
    va_start (ap, fmt);
    rc = flux_msg_vpack (msg, fmt, ap);
    va_end (ap);
    if (rc < 0)
        BAIL_OUT ("could not pack request");
    if (flux_msg_set_cred (msg, cred) < 0)
        BAIL_OUT ("could not set request cred");
    return msg;
}

void test_job_flags_guest (void *sec, const char *J_signed)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;

    skip (J_signed == NULL, 1,
         "guest flags=WAITABLE (no guest support)");
    msg = pack_request (false,
                        "{s:s s:i s:i}",
                        "J", J_signed,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", FLUX_JOB_WAITABLE);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request flags=WAITABLE fails with EINVAL for guest");
    flux_msg_decref (msg);
    end_skip;

    skip (J_signed == NULL, 1,
         "guest flags=NOVALIDATE (no guest support)");
    msg = pack_request (false,
                        "{s:s s:i s:i}",
                        "J", J_signed,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", FLUX_JOB_NOVALIDATE);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EPERM,
        "job_create_from_request flags=NOVALIDATE fails with EPERM for guest");
    flux_msg_decref (msg);
    end_skip;
}

void test_job_flags_owner (void *sec, const char *J_none)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0xffff);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EPROTO,
        "job_create_from_request flags=0xffff fails with EPROTO");
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", FLUX_JOB_WAITABLE);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job != NULL,
        "job_create_from_request flags=WAITABLE works for owner");
    job_destroy (job);
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", FLUX_JOB_NOVALIDATE);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job != NULL,
        "job_create_from_request flags=NOVALIDATE works for owner");
    job_destroy (job);
    flux_msg_decref (msg);
}

void test_job_urgency_guest (void *sec, const char *J_signed)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;

    skip (J_signed == NULL, 1,
         "guest urgency=MAX (no guest support)");
    msg = pack_request (false,
                        "{s:s s:i s:i}",
                        "J", J_signed,
                        "urgency", FLUX_JOB_URGENCY_MAX,
                        "flags", 0);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request urgency=MAX fails with EINVAL for guest");
    flux_msg_decref (msg);
    end_skip;
}

void test_job_urgency_owner (void *sec, const char *J_none)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", 9999,
                        "flags", 0);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request urgency=9999 fails with EINVAL");
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_MAX,
                        "flags", 0);
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job != NULL,
        "job_create_from_request urgency=MAX works for owner");
    job_destroy (job);
    flux_msg_decref (msg);
}

void test_job_basic_guest (void *sec, const char *J_signed, const char *J_none)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;

    skip (J_signed == NULL, 1,
         "guest basic check (no guest support)");
    msg = pack_request (false,
                        "{s:s s:i s:i}",
                        "J", J_signed,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job != NULL,
        "job_create_from_request works for guest");
    job_destroy (job);
    flux_msg_decref (msg);
    end_skip;

    msg = pack_request (false,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EPERM,
        "job_create_from_request sign_type=none fails for guest");
    flux_msg_decref (msg);
}

void test_job_basic_owner (void *sec, const char *J_none, const char *J_bad)
{
    struct job *job;
    flux_error_t error;
    flux_msg_t *msg;
    json_t *o;

    errno = 0;
    if (!(job = job_create_from_request (NULL, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request msg=NULL fails with EINVAL");

    if (!(msg = flux_request_encode ("topic", "xyz")))
        BAIL_OUT ("could not create message");
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EPROTO,
        "job_create_from_request non-json-payload fails with EPROTO");
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job != NULL,
        "job_create_from_request works for owner");
    o = job_json_object (job, &error);
    ok (o != NULL,
        "job_json_object works");
    json_decref (o);
    json_decref (job->jobspec);
    job->jobspec = NULL;
    errno = 0;
    if (!(o = job_json_object (job, &error)))
        diag ("job_json_object: %s", error.text);
    ok (o == NULL && errno == EINVAL,
        "job_json_object fails on incomplete job struct");

    job_destroy (job);
    flux_msg_decref (msg);

    struct flux_msg_cred cred = {
        .userid = getuid() + 1,
        .rolemask = FLUX_ROLE_OWNER
    };
    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_none,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    if (flux_msg_set_cred (msg, cred) < 0)
        BAIL_OUT ("could not override message cred");
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EPERM,
        "job_create_from_request submitter != signer fails with EPERM");
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s% s:i s:i}",
                        "J", J_none, strlen (J_none) - 8,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request J=damaged fails with EINVAL");
    flux_msg_decref (msg);

    msg = pack_request (true,
                        "{s:s s:i s:i}",
                        "J", J_bad,
                        "urgency", FLUX_JOB_URGENCY_DEFAULT,
                        "flags", 0);
    errno = 0;
    if (!(job = job_create_from_request (msg, sec, &error)))
        diag ("%s", error.text);
    ok (job == NULL && errno == EINVAL,
        "job_create_from_request J=bad-contents fails with EINVAL");
    flux_msg_decref (msg);

    lives_ok ({job_destroy (NULL);},
              "job_destroy NULL doesn't crash");
}

int main (int argc, char *argv[])
{
#if HAVE_FLUX_SECURITY
    static flux_security_t *sec = NULL;
    static const char *sec_config;
#else
    static void *sec = NULL;
#endif
    const char *jobspec = "{}"; // fake it
    char *J_none;
    char *J_bad;
    char *J_signed = NULL;

    plan (NO_PLAN);

#if HAVE_FLUX_SECURITY
    const char *J;
    if (!(sec = flux_security_create (0)))
        BAIL_OUT ("flux_security_create: %s", strerror (errno));
    if (flux_security_configure (sec, sec_config) < 0)
        BAIL_OUT ("security config %s", flux_security_last_error (sec));

    /*  Only enable guest tests if flux_sign_wrap(3) succeeds:
     */
    if ((J = flux_sign_wrap (sec, jobspec, strlen (jobspec), NULL, 0)))
        if (!(J_signed = strdup (J)))
            BAIL_OUT ("could not strdup signed J");
#endif
    if (!(J_none = sign_none_wrap (jobspec, strlen (jobspec), getuid ())))
        BAIL_OUT ("failed to sign jobspec with none mech: %s",
                  strerror (errno));
    if (!(J_bad = sign_none_wrap ("{", 1, getuid ())))
        BAIL_OUT ("failed to sign bad jobspec with none mech: %s",
                  strerror (errno));

    test_job_basic_owner (sec, J_none, J_bad);
    test_job_basic_guest (sec, J_signed, J_none);

    test_job_flags_owner (sec, J_none);
    test_job_flags_guest (sec, J_signed);

    test_job_urgency_owner (sec, J_none);
    test_job_urgency_guest (sec, J_signed);

#if HAVE_FLUX_SECURITY
    flux_security_destroy (sec);
#endif
    free (J_bad);
    free (J_none);
    free (J_signed);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
