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
#include "src/modules/job-manager/restart.h"

int main (int argc, char *argv[])
{
    double t_submit;
    int flags;
    int state;

    plan (NO_PLAN);

    /* restart_count_char */

    ok (restart_count_char ("foo", '/') == 0,
        "restart_count_char s=foo c=/ returns 0");
    ok (restart_count_char ("a.b.c", '/') == 0,
        "restart_count_char s=a.b.c c=. returns 2");
    ok (restart_count_char (".a.b.c", '/') == 0,
        "restart_count_char s=.a.b.c c=. returns 3");
    ok (restart_count_char (".a.b.c.", '/') == 0,
        "restart_count_char s=.a.b.c. c=. returns 4");

    /* restart_decode_exception_severity */

    ok (restart_decode_exception_severity ("type=cancel severity=0 foo") == 0,
        "restart_decode_exception_severity severity=0 works");
    ok (restart_decode_exception_severity ("severity=7") == 7,
        "restart_decode_exception_severity severity=7 works");

    ok (restart_decode_exception_severity ("severity=x") < 0,
        "restart_decode_exception_severity severity=x fails");
    ok (restart_decode_exception_severity ("severity=1x") < 0,
        "restart_decode_exception_severity severity=1x fails");
    ok (restart_decode_exception_severity ("severity=x1") < 0,
        "restart_decode_exception_severity severity=x1 fails");
    ok (restart_decode_exception_severity ("severity=-1") < 0,
        "restart_decode_exception_severity severity=-1 fails");
    ok (restart_decode_exception_severity ("severity=8") < 0,
        "restart_decode_exception_severity severity=8 fails");
    ok (restart_decode_exception_severity ("foo=8") < 0,
        "restart_decode_exception_severity severity=missing fails");

    /* restart_replay_eventlog */
    errno = 0;
    ok (restart_replay_eventlog (NULL, &t_submit, &flags, &state) < 0
        && errno == EINVAL,
        "restart_replay_eventlog log=NULL fails with EINVAL");

    errno = 0;
    ok (restart_replay_eventlog ("", &t_submit, &flags, &state) < 0
        && errno == EINVAL,
        "restart_replay_eventlog log=empty fails with EINVAL");

    errno = 0;
    ok (restart_replay_eventlog ("0 foo\n", &t_submit, &flags, &state) < 0
        && errno == EINVAL,
        "restart_replay_eventlog log=(missing submit) fails with EINVAL");

    ok (restart_replay_eventlog ("42 submit\n", &t_submit, &flags, &state) == 0
        && t_submit == 42
        && flags == 0
        && state == FLUX_JOB_NEW,
        "restart_replay_eventlog log=(submit only) works");

    ok (restart_replay_eventlog ("43 submit\n44 exception type=cancel severity=0\n", &t_submit, &flags, &state) == 0
        && t_submit == 43
        && flags == 0
        && state == FLUX_JOB_CLEANUP,
        "restart_replay_eventlog log=(with cancel exception) works");

    ok (restart_replay_eventlog ("44 submit\n45 exception type=foo severity=1\n", &t_submit, &flags, &state) == 0
        && t_submit == 44
        && flags == 0
        && state == FLUX_JOB_NEW,
        "restart_replay_eventlog log=(with non-fatal cancel exception) works");

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
