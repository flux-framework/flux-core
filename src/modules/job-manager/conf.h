/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_CONF_H
#define _FLUX_JOB_MANAGER_CONF_H

#include <stdbool.h>
#include <flux/core.h>

#include "job-manager.h"

/* Return value:
 *   0=success, one-shot
 *  -1=failure (set 'error' but not errno)
 *   1=success, continue to invoke callback on config updates
 */
typedef int (*conf_update_f)(const flux_conf_t *conf,
                             flux_error_t *error,
                             void *arg);

struct conf *conf_create (struct job_manager *ctx, flux_error_t *error);
void conf_destroy (struct conf *conf);

/* Immediately call 'cb' on current config object, and then on config updates
 * as indicated by initial callback's return value (see above).
 *
 * The new configuration is committed only after every callback accepts it.
 * A callback that fails leaves itself and the callbacks ahead of it holding
 * a configuration the instance never adopted, so those callbacks are rolled
 * back by replaying the committed configuration to them. Replay works for
 * the failed callback too: it accepted that configuration once already.
 *
 * A registrant whose state is a pure function of the configuration passes a
 * NULL 'rollback' and the replay goes to 'cb'. One that carries state the
 * configuration does not describe supplies 'rollback', called in place of
 * 'cb' on that path. It returns 0 or -1 only.
 *
 * 'name' identifies the registrant in log messages and must be a string
 * constant.
 */
int conf_register_callback (struct conf *conf,
                            flux_error_t *error,
                            const char *name,
                            conf_update_f cb,
                            conf_update_f rollback,
                            void *arg);
void conf_unregister_callback (struct conf *conf, conf_update_f cb);

#endif /* ! _FLUX_JOB_MANAGER_CONF_H */

// vi:ts=4 sw=4 expandtab
