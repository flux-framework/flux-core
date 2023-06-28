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
#include <stdbool.h>
#include <string.h>
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "state.h"

struct state_tab {
    const char *name;
    int state;
    bool reverse;
};

static struct state_tab states[] = {
    { "unknown", STATE_UNKNOWN, true },
    { "xyz", STATE_UNKNOWN, false },
    { NULL, STATE_UNKNOWN, false },
    { "activating", STATE_ACTIVATING, true },
    { "active", STATE_ACTIVE, true },
    { "deactivating", STATE_DEACTIVATING, true },
    { "inactive", STATE_INACTIVE, true },
    { "failed", STATE_FAILED, true },
};

static struct state_tab subs[] = {
    { "unknown", SUBSTATE_UNKNOWN, true },
    { "xyz", SUBSTATE_UNKNOWN, false },
    { NULL, SUBSTATE_UNKNOWN, false },
    { "dead", SUBSTATE_DEAD, true },
    { "start", SUBSTATE_START, true },
    { "running", SUBSTATE_RUNNING, true },
    { "exited", SUBSTATE_EXITED, true },
    { "failed", SUBSTATE_FAILED, true },
};


int main (int ac, char *av[])
{
    plan (NO_PLAN);

    for (int i = 0; i < ARRAY_SIZE (states); i++) {
        ok (sdexec_strtostate (states[i].name) == states[i].state,
            "sdexec_strtostate %s works", states[i].name);
        if (states[i].reverse) {
            ok (streq (sdexec_statetostr (states[i].state), states[i].name),
                "sdexec_statetostr %s works", states[i].name);
        }
    }
    for (int i = 0; i < ARRAY_SIZE (subs); i++) {
        ok (sdexec_strtosubstate (subs[i].name) == subs[i].state,
            "sdexec_strtosubstate %s works", subs[i].name);
        if (subs[i].reverse) {
            ok (streq (sdexec_substatetostr (subs[i].state), subs[i].name),
                "sdexec_substatetostr %s works", subs[i].name);
        }
    }

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
