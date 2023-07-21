/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_STATE_H
#define _LIBSDEXEC_STATE_H

typedef enum {
    STATE_UNKNOWN,
    STATE_INACTIVE,
    STATE_ACTIVATING,
    STATE_ACTIVE,
    STATE_DEACTIVATING,
    STATE_FAILED,
} sdexec_state_t;

typedef enum {
    SUBSTATE_UNKNOWN,
    SUBSTATE_DEAD,
    SUBSTATE_START,
    SUBSTATE_RUNNING,
    SUBSTATE_EXITED,
    SUBSTATE_FAILED,
} sdexec_substate_t;

const char *sdexec_statetostr (sdexec_state_t state);
const char *sdexec_substatetostr (sdexec_substate_t substate);

sdexec_state_t sdexec_strtostate (const char *s);
sdexec_substate_t sdexec_strtosubstate (const char *s);

#endif /* !_LIBSDEXEC_STATE_H */

// vi:ts=4 sw=4 expandtab
