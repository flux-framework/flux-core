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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libmissing/macros.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/aux.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

#include "property.h"
#include "list.h"
#include "unit.h"

struct unit {
    char *name;
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
        free (unit->name);
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
        return unit->name;
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

static bool is_systemd_exit_code (int code)
{
    if (code < 200 || code > 243)
        return false;
    return true;
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
            !is_systemd_exit_code (unit->exec_main_status))
            return true;
    }
    return false;
}

bool sdexec_unit_has_failed (struct unit *unit)
{
    if (unit) {
        if (unit->exec_main_status_is_set &&
            is_systemd_exit_code (unit->exec_main_status))
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
            && !is_systemd_exit_code (unit->exec_main_status))
            || unit->substate == SUBSTATE_START) {
            return true;
        }
    }
    return false;
}

static const char *unit_path_prefix = "/org/freedesktop/systemd1/unit/";

static bool is_label_char (char c)
{
    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
}

static int hexval (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Escape 's' as a D-Bus object path element, as systemd's
 * bus_label_escape() does: each byte that is not [A-Za-z0-9] becomes _XX
 * (lowercase hex), a leading digit is also escaped, and the empty string
 * becomes "_".  If 'glob' is true, '*' is passed through.
 */
static char *unit_path_escape (const char *s, bool glob)
{
    size_t len = strlen (unit_path_prefix);
    char *path;
    char *cp;

    if (!(path = malloc (len + strlen (s) * 3 + 2)))
        return NULL;
    cp = stpcpy (path, unit_path_prefix);
    if (*s == '\0')
        *cp++ = '_';
    for (const char *p = s; *p != '\0'; p++) {
        bool leading_digit = (p == s && *p >= '0' && *p <= '9');
        if ((is_label_char (*p) && !leading_digit) || (glob && *p == '*'))
            *cp++ = *p;
        else
            cp += sprintf (cp, "_%02x", (unsigned char)*p);
    }
    *cp = '\0';
    return path;
}

char *sdexec_unit_path_encode (const char *name)
{
    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    return unit_path_escape (name, false);
}

char *sdexec_unit_path_glob (const char *name_glob)
{
    if (!name_glob || strpbrk (name_glob, "?[\\")) {
        errno = EINVAL;
        return NULL;
    }
    return unit_path_escape (name_glob, true);
}

char *sdexec_unit_path_decode (const char *path)
{
    const char *s;
    char *name;
    char *cp;

    if (!path || !strstarts (path, unit_path_prefix)) {
        errno = EINVAL;
        return NULL;
    }
    s = path + strlen (unit_path_prefix);
    if (*s == '\0' || strchr (s, '/')) {
        errno = EINVAL;
        return NULL;
    }
    if (!(name = malloc (strlen (s) + 1)))
        return NULL;
    cp = name;
    if (!streq (s, "_")) {
        while (*s != '\0') {
            int hi, lo;
            if (*s == '_'
                && (hi = hexval (s[1])) >= 0
                && (lo = hexval (s[2])) >= 0) {
                *cp++ = hi << 4 | lo;
                s += 3;
            }
            else
                *cp++ = *s++;
        }
    }
    *cp = '\0';
    return name;
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
    if (!(unit->name = strdup (name))
        || !(unit->path = sdexec_unit_path_encode (name)))
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
