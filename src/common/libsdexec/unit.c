/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* unit.c - translate unit property updates to unit object changes
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <flux/core.h>

#include "src/common/libmissing/macros.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/basename.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "property.h"
#include "list.h"
#include "unit.h"

struct unit {
    char *path;
    sdexec_state_t state;
    sdexec_substate_t substate;
    pid_t exec_main_pid;
    int exec_main_code;
    int exec_main_status;
    bool exec_main_pid_is_set;
    bool exec_main_status_is_set;

    struct aux_item *aux;
};

void sdexec_unit_destroy (struct unit *unit)
{
    if (unit) {
        int saved_errno = errno;
        aux_destroy (&unit->aux);
        free (unit->path);
        free (unit);
        errno = saved_errno;
    }
}

void *sdexec_unit_aux_get (struct unit *unit, const char *name)
{
    if (!unit) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (unit->aux, name);
}

int sdexec_unit_aux_set (struct unit *unit,
                         const char *name,
                         void *aux,
                         flux_free_f destroy)
{
    if (!unit) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&unit->aux, name, aux, destroy);
}

const char *sdexec_unit_name (struct unit *unit)
{
    if (unit)
        return basename_simple (unit->path);
    return "internal error: unit is null";
}

const char *sdexec_unit_path (struct unit *unit)
{
    if (unit)
        return unit->path;
    return "internal error: unit is null";
}

pid_t sdexec_unit_pid (struct unit *unit)
{
    if (unit && unit->exec_main_pid_is_set)
        return unit->exec_main_pid;
    return -1;
}

sdexec_state_t sdexec_unit_state (struct unit *unit)
{
    if (unit)
        return unit->state;
    return STATE_UNKNOWN;
}

sdexec_substate_t sdexec_unit_substate (struct unit *unit)
{
    if (unit)
        return unit->substate;
    return SUBSTATE_UNKNOWN;
}

int sdexec_unit_wait_status (struct unit *unit)
{
    if (sdexec_unit_has_finished (unit)) {
        if (unit->exec_main_code == CLD_KILLED)
            return __W_EXITCODE (0, unit->exec_main_status);
        else
            return __W_EXITCODE (unit->exec_main_status, 0);
    }
    return -1;
}

int sdexec_unit_systemd_error (struct unit *unit)
{
    if (sdexec_unit_has_failed (unit))
        return unit->exec_main_status;
    return -1;
}

bool sdexec_unit_has_finished (struct unit *unit)
{
    if (unit) {
        if (unit->exec_main_status_is_set &&
            unit->exec_main_status < 200) // systemd errors are [200-243]
            return true;
    }
    return false;
}

bool sdexec_unit_has_failed (struct unit *unit)
{
    if (unit) {
        if (unit->exec_main_status_is_set &&
            unit->exec_main_status >= 200) // systemd errors are [200-243]
            return true;
    }
    return false;
}

bool sdexec_unit_has_started (struct unit *unit)
{
    if (unit) {
        if (!unit->exec_main_pid_is_set)
            return false;
        /* Process was started if it's got an exit status,
         * unless the exit status is a systemd error [200-243].
         */
        if ((unit->exec_main_status_is_set
            && unit->exec_main_status < 200)
            || unit->substate == SUBSTATE_START) {
            return true;
        }
    }
    return false;
}

struct unit *sdexec_unit_create (const char *name)
{
    struct unit *unit;

    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(unit = calloc (1, sizeof (*unit))))
        return NULL;
    if (asprintf (&unit->path, "/org/freedesktop/systemd1/unit/%s", name) < 0)
        goto error;
    unit->state = STATE_UNKNOWN;
    unit->substate = SUBSTATE_UNKNOWN;
    return unit;
error:
    sdexec_unit_destroy (unit);
    return NULL;
}

bool sdexec_unit_update (struct unit *unit, json_t *dict)
{
    json_int_t i;
    json_int_t j;
    const char *s;
    int changes = 0;

    if (!unit || !dict)
        return false;

    /* The pid is for the forked child and so its availability does not
     * necessarily mean the exec has succeeded.
     */
    if (sdexec_property_dict_unpack (dict, "ExecMainPID", "I", &i) == 0
        && !unit->exec_main_pid_is_set) {
        unit->exec_main_pid = i;
        unit->exec_main_pid_is_set = true;
        changes++;
    }
    /* These seem to be set as a pair, and appear early with values of zero,
     * which is a valid status but not CLD_* code.  So don't set either unless
     * the code is valid.  On exec failure, code=1 (CLD_EXITED), status=203.
     */
    if (sdexec_property_dict_unpack (dict, "ExecMainCode", "I", &i) == 0
        && sdexec_property_dict_unpack (dict, "ExecMainStatus", "I", &j) == 0
        && !unit->exec_main_status_is_set
        && i > 0) {
        unit->exec_main_code = i;
        unit->exec_main_status = j;
        unit->exec_main_status_is_set = true;
        changes++;
    }
    if (sdexec_property_dict_unpack (dict, "SubState", "s", &s) == 0) {
        sdexec_substate_t substate  = sdexec_strtosubstate (s);
        if (unit->substate != substate) {
            unit->substate = substate;
            changes++;
        }
    }
    if (sdexec_property_dict_unpack (dict, "ActiveState", "s", &s) == 0) {
        sdexec_state_t state = sdexec_strtostate (s);
        if (unit->state != state) {
            unit->state = state;
            changes++;
        }
    }
    return (changes > 0 ? true : false);
}

bool sdexec_unit_update_frominfo (struct unit *unit, struct unit_info *info)
{
    sdexec_state_t state;
    sdexec_substate_t substate;
    int changes = 0;

    if (!unit || !info)
        return false;

    state = sdexec_strtostate (info->active_state);
    substate = sdexec_strtosubstate (info->sub_state);
    if (unit->state != state) {
        unit->state = state;
        changes++;
    }
    if (unit->substate != substate) {
        unit->substate = substate;
        changes++;
    }
    return (changes > 0 ? true : false);
}

// vi:ts=4 sw=4 expandtab
