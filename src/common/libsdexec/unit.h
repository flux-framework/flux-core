/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_UNIT_H
#define _LIBSDEXEC_UNIT_H

#include <sys/types.h>
#include <stdint.h>
#include <jansson.h>
#include <flux/core.h>

#include "list.h"
#include "state.h"

/* Create/destroy a unit object.
 */
/* Convert a unit name to its systemd D-Bus object path, e.g.
 * "foo.service" => "/org/freedesktop/systemd1/unit/foo_2eservice".
 * The caller must free the result.
 */
char *sdexec_unit_path_encode (const char *name);

/* Convert a systemd unit object path to a unit name.
 * Fails with EINVAL if 'path' is not a unit object path.
 * The caller must free the result.
 */
char *sdexec_unit_path_decode (const char *path);

/* Convert a fnmatch(3) glob on unit names to a glob on unit object paths.
 * Only the '*' wildcard is supported, since one name character may encode
 * to three path characters.  The result may match paths of units whose
 * names do not match 'name_glob', so callers should check decoded names.
 * Fails with EINVAL if the glob contains '?', '[', or a backslash.
 * The caller must free the result.
 */
char *sdexec_unit_path_glob (const char *name_glob);

struct unit *sdexec_unit_create (const char *name);
void sdexec_unit_destroy (struct unit *unit);

/* Update unit object with property dict from sdexec_property_changed_dict()
 * or sdexec_property_get_all_dict().  Return true if there was a change,
 * false if the update was a no-op with respect to the unit object.
 */
bool sdexec_unit_update (struct unit *unit, json_t *property_dict);

/* Like above but update unit with info from sdexec_list_units_next()
 */
bool sdexec_unit_update_frominfo (struct unit *unit, struct unit_info *info);

/* Attach arbitrary data to unit.
 */
void *sdexec_unit_aux_get (struct unit *unit, const char *name);
int sdexec_unit_aux_set (struct unit *unit,
                         const char *name,
                         void *aux,
                         flux_free_f destroy);

/* accessors */
sdexec_state_t sdexec_unit_state (struct unit *unit);
sdexec_substate_t sdexec_unit_substate (struct unit *unit);
pid_t sdexec_unit_pid (struct unit *unit);
const char *sdexec_unit_path (struct unit *unit);
const char *sdexec_unit_name (struct unit *unit);

// returns wait(2) status if unit_has_finished() == true, else -1.
int sdexec_unit_wait_status (struct unit *unit);

// returns error code if unit_has_failed() == true, else -1
int sdexec_unit_systemd_error (struct unit *unit);

bool sdexec_unit_has_started (struct unit *unit);
bool sdexec_unit_has_finished (struct unit *unit);
bool sdexec_unit_has_failed (struct unit *unit);

#endif /* !_LIBSDEXEC_UNIT_H */

// vi:ts=4 sw=4 expandtab
