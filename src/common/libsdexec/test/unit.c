/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <sys/wait.h>
#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "list.h"
#include "unit.h"

void test_init (void)
{
    struct unit *unit;
    const char *s;

    unit = sdexec_unit_create ("foo.service");
    ok (unit != NULL,
        "sdexec_unit_create works");
    ok (sdexec_unit_state (unit) == STATE_UNKNOWN,
        "initial state is UNKNOWN");
    ok (sdexec_unit_substate (unit) == SUBSTATE_UNKNOWN,
        "initial substate is UNKNOWN");
    ok (sdexec_unit_pid (unit) == -1,
        "initial pid is -1");
    s = sdexec_unit_name (unit);
    ok (s && streq (s, "foo.service"),
        "sdexec_unit_name returns original name");
    s = sdexec_unit_path (unit);
    ok (s != NULL && streq (s, "/org/freedesktop/systemd1/unit/foo.service"),
        "sdexec_unit_path returns expected path");
    ok (sdexec_unit_has_started (unit) == false,
        "sdexec_unit_has_started returns false");
    ok (sdexec_unit_has_finished (unit) == false,
        "sdexec_unit_has_finished returns false");
    ok (sdexec_unit_wait_status (unit) == -1,
        "sdexec_unit_wait_status returns -1");
    ok (sdexec_unit_systemd_error (unit) == -1,
        "sdexec_unit_systemd_error returns -1");

    sdexec_unit_destroy (unit);
    ok (true, "sdexec_unit_destroy called");
}

void test_update (void)
{
    struct unit_info info = {
        .active_state = "active",
        .sub_state = "start",
    };
    struct unit *unit;
    json_t *dict_pid;
    json_t *dict_exit;

    if (!(unit = sdexec_unit_create ("foo.service")))
        BAIL_OUT ("could not create unit object for testing");
    if (!(dict_pid = json_pack ("{s:[si]}", "ExecMainPID", "I", 42)))
        BAIL_OUT ("could not create property dict with MainExitPid");
    if (!(dict_exit = json_pack ("{s:[si] s:[si]}",
                                 "ExecMainCode", "I", CLD_EXITED,
                                 "ExecMainStatus", "I", 0)))
        BAIL_OUT ("could not create property dict with"
                  " ExecMainCode, ExecMainStatus for testing");

    ok (sdexec_unit_update (unit, dict_pid) == true,
        "sdexec_unit_update ExecMainPID=42 returns true");
    ok (sdexec_unit_pid (unit) == 42,
        "sdexec_unit_pid returns 42");

    ok (sdexec_unit_update_frominfo (unit, &info) == true,
        "sdexec_unit_update_frominfo active,start returns true");
    ok (sdexec_unit_has_started (unit) == true,
        "sdexec_unit_has_started returns true");

    ok (sdexec_unit_update (unit, dict_exit) == true,
        "sdexec_unit_update ExecMainCode=CLD_EXITED ExecMainStatus=0"
        " returns true");
    ok (sdexec_unit_has_finished (unit) == true,
        "sdexec_unit_has_finished returns true");
    ok (sdexec_unit_has_failed (unit) == false,
        "sdexec_unit_has_finished returns true");
    ok (sdexec_unit_wait_status (unit) == 0,
        "sdexec_unit_wait_status returns 0");

    json_decref (dict_exit);
    json_decref (dict_pid);
    sdexec_unit_destroy (unit);
}

void test_inval (void)
{
    struct unit *unit;
    json_t *dict;
    struct unit_info info = {
        .active_state = "active",
        .sub_state = "start",
    };

    if (!(unit = sdexec_unit_create ("foo.service")))
        BAIL_OUT ("could not create unit object for testing");
    if (!(dict = json_pack ("{s:[si]}", "foo", "i", 42)))
        BAIL_OUT ("could not create property dict for testing");

    errno = 0;
    ok (sdexec_unit_create (NULL) == NULL && errno == EINVAL,
        "sdexec_unit_create name=NULL fails with EINVAL");

    ok (sdexec_unit_state (NULL) == STATE_UNKNOWN,
        "sdexec_unit_state unit=NULL is UNKNOWN");
    ok (sdexec_unit_substate (NULL) == SUBSTATE_UNKNOWN,
        "sdexec_unit_substate unit=NULL is UNKNOWN");
    ok (sdexec_unit_name (NULL) != NULL,
        "sdexec_unit_name unit=NULL returns non-NULL");
    ok (sdexec_unit_path (NULL) != NULL,
        "sdexec_unit_path unit=NULL returns non-NULL");
    ok (sdexec_unit_has_started (NULL) == false,
        "sdexec_unit_has_started unit=NULL returns false");
    ok (sdexec_unit_has_finished (NULL) == false,
        "sdexec_unit_has_finished unit=NULL returns false");
    ok (sdexec_unit_wait_status (NULL) == -1,
        "sdexec_unit_wait_status unit=NULL returns -1");
    ok (sdexec_unit_systemd_error (NULL) == -1,
        "sdexec_unit_systemd_error unit=NULL returns -1");
    ok (sdexec_unit_update (NULL, dict) == false,
        "sdexec_unit_update unit=NULL returns false");
    ok (sdexec_unit_update (unit, NULL) == false,
        "sdexec_unit_update dict=NULL returns false");

    ok (sdexec_unit_update_frominfo (NULL, &info) == false,
        "sdexec_unit_update_frominfo unit=NULL returns false");
    ok (sdexec_unit_update_frominfo (unit, NULL) == false,
        "sdexec_unit_update_frominfo info=NULL returns false");

    errno = 0;
    ok (sdexec_unit_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "sdexec_unit_aux_set unit=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_unit_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "sdexec_unit_aux_get unit=NULL fails with EINVAL");

    lives_ok ({sdexec_unit_destroy (NULL);},
              "sdexec_unit_destroy unit=NULL doesn't crash");

    json_decref (dict);
    sdexec_unit_destroy (unit);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_init ();
    test_update ();
    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
