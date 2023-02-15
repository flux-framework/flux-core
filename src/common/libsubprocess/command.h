/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_CMD_H
#define _SUBPROCESS_CMD_H

#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "subprocess.h"

/*
 *  Internal only flux_cmd_t interfaces
 */

/*
 *  Return JSON representation of command object `cmd`
 */
json_t *cmd_tojson (const flux_cmd_t *cmd);

/*
 *  Return a newly allocated flux_cmd_t from a JSON representation.
 *   Returns NULL on failure.
 *   If non-NULL, any jansson decode errors are returned in *errp.
 */
flux_cmd_t *cmd_fromjson (json_t *o, json_error_t *errp);

/*
 *  Return environment for flux_cmd_t as a NULL terminated string array.
 */
char **flux_cmd_env_expand (flux_cmd_t *cmd);

/*
 *  Return argument vector for flux_cmd_t as NULL terminated string array.
 */
char **flux_cmd_argv_expand (flux_cmd_t *cmd);

/*
 *  Set an entirely new environment, discarding internal one.
 */
int flux_cmd_set_env (flux_cmd_t *cmd, char **env);

/*
 *  Return list of channels.  Should not be destryed by caller.
 */
zlist_t *flux_cmd_channel_list (flux_cmd_t *cmd);

/*
 * Find opts that contain a specific substring.  Returns 1 if
 * substrings found, 0 if not.
 */
int flux_cmd_find_opts (const flux_cmd_t *cmd, const char **substrings);

#endif /* !_SUBPROCESS_CMD_H */
