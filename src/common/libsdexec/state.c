/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* state.c - unit state/substate
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "state.h"

struct state_tab {
    const char *name;
    int state;
};

static struct state_tab states[] = {
    { "unknown", STATE_UNKNOWN },
    { "activating", STATE_ACTIVATING },
    { "active", STATE_ACTIVE },
    { "deactivating", STATE_DEACTIVATING },
    { "inactive", STATE_INACTIVE },
    { "failed", STATE_FAILED },
};

static struct state_tab substates[] = {
    { "unknown", SUBSTATE_UNKNOWN },
    { "dead", SUBSTATE_DEAD },
    { "start", SUBSTATE_START },
    { "running", SUBSTATE_RUNNING },
    { "exited", SUBSTATE_EXITED },
    { "failed", SUBSTATE_FAILED },
};

static int strtostate (const char *s,
                       struct state_tab *tab,
                       size_t len)
{
    if (s) {
        for (int i = 0; i < len; i++) {
            if (streq (tab[i].name, s))
                return tab[i].state;
        }
    }
    return tab[0].state;
}

sdexec_state_t sdexec_strtostate (const char *s)
{
    return strtostate (s, states, ARRAY_SIZE (states));
}

sdexec_substate_t sdexec_strtosubstate (const char *s)
{
    return strtostate (s, substates, ARRAY_SIZE (substates));
}

static const char *statetostr (int state, struct state_tab *tab, size_t len)
{
    for (int i = 0; i < len; i++) {
        if (tab[i].state == state)
            return tab[i].name;
    }
    return tab[0].name;
}

const char *sdexec_statetostr (sdexec_state_t state)
{
    return statetostr (state, states, ARRAY_SIZE (states));
}

const char *sdexec_substatetostr (sdexec_substate_t substate)
{
    return statetostr (substate, substates, ARRAY_SIZE (substates));
}

// vi:ts=4 sw=4 expandtab
