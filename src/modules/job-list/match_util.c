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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"

#include "match_util.h"

int array_to_states_bitmask (json_t *values, flux_error_t *errp)
{
    int states = 0;
    json_t *entry;
    size_t index;
    int valid_states = (FLUX_JOB_STATE_NEW
                        | FLUX_JOB_STATE_PENDING
                        | FLUX_JOB_STATE_RUNNING
                        | FLUX_JOB_STATE_INACTIVE);

    json_array_foreach (values, index, entry) {
        flux_job_state_t state;
        if (json_is_string (entry)) {
            const char *statestr = json_string_value (entry);
            if (flux_job_strtostate (statestr, &state) < 0) {
                errprintf (errp,
                           "invalid states value '%s' specified",
                           statestr);
                return -1;
            }
        }
        else if (json_is_integer (entry)) {
            state = json_integer_value (entry);
            if (state & ~valid_states) {
                errprintf (errp,
                           "invalid states value '%Xh' specified",
                           state);
                return -1;
            }
        }
        else {
            errprintf (errp, "states value invalid type");
            return -1;
        }
        states |= state;
    }
    return states;
}

int array_to_results_bitmask (json_t *values, flux_error_t *errp)
{
    int results = 0;
    json_t *entry;
    size_t index;
    int valid_results = (FLUX_JOB_RESULT_COMPLETED
                         | FLUX_JOB_RESULT_FAILED
                         | FLUX_JOB_RESULT_CANCELED
                         | FLUX_JOB_RESULT_TIMEOUT);

    json_array_foreach (values, index, entry) {
        flux_job_result_t result;
        if (json_is_string (entry)) {
            const char *resultstr = json_string_value (entry);
            if (flux_job_strtoresult (resultstr, &result) < 0) {
                errprintf (errp,
                           "invalid results value '%s' specified",
                           resultstr);
                return -1;
            }
        }
        else if (json_is_integer (entry)) {
            result = json_integer_value (entry);
            if (result & ~valid_results) {
                errprintf (errp,
                           "invalid results value '%Xh' specified",
                           result);
                return -1;
            }
        }
        else {
            errprintf (errp, "results value invalid type");
            return -1;
        }
        results |= result;
    }
    return results;
}

/* vi: ts=4 sw=4 expandtab
 */
