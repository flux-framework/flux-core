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
#include <czmq.h>

#include "subprocess.h"

/*
 *  Internal only flux_cmd_t interfaces
 */

/*
 *  Return JSON string representation of command object `cmd`
 */
char * flux_cmd_tojson (const flux_cmd_t *cmd);

/*
 *  Return a newly allocated flux_cmd_t from a JSON string representation.
 *   Returns NULL on failure.
 *   If non-NULL, any jansson decode errors are returned in *errp.
 */
flux_cmd_t *flux_cmd_fromjson (const char *json_str, json_error_t *errp);

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

#endif /* !_SUBPROCESS_CMD_H */
