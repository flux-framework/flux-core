/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
 \************************************************************/

/* barrier.c - time a PMIx_Fence() with no data
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <pmix.h>
#include <stdio.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"

int main (int argc, char **argv)
{
    pmix_proc_t self;
    int rc;

    /* Initialize
     * Use the rank as a prefix for any log messages (once known).
     */
    char name[512];
    if ((rc = PMIx_Init (&self, NULL, 0)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Init: %s", PMIx_Error_string (rc));
    snprintf (name, sizeof (name), "%s.%d", self.nspace, self.rank);
    log_init (name);
    if (self.rank == 0)
        log_msg ("completed PMIx_Init.");

    /* Get the size and print it so we know the test wired up.
     */
    pmix_proc_t proc;
    pmix_value_t *valp;
    strncpy (proc.nspace, self.nspace, PMIX_MAX_NSLEN);
    proc.nspace[PMIX_MAX_NSLEN] = '\0';
    proc.rank = PMIX_RANK_WILDCARD;
    if ((rc = PMIx_Get (&proc, PMIX_JOB_SIZE, NULL, 0, &valp)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Get %s: %s", PMIX_JOB_SIZE, PMIx_Error_string (rc));
    if (self.rank == 0)
        log_msg ("there are %d tasks",
                 valp->type == PMIX_UINT32 ? valp->data.uint32 : -1);
    PMIX_VALUE_RELEASE (valp);

    /* Time the fence
     */
    struct timespec t;
    monotime (&t);
    if ((rc = PMIx_Fence (NULL, 0, NULL, 0)))
        log_msg_exit ("PMIx_Fence: %s", PMIx_Error_string (rc));
    if (self.rank == 0)
        log_msg ("completed barrier in %0.3fs.", monotime_since (t) / 1000);

    if ((rc = PMIx_Finalize (NULL, 0)))
        log_msg_exit ("PMIx_Finalize: %s", PMIx_Error_string (rc));
    if (self.rank == 0)
        log_msg ("completed PMIx_Finalize.");
    return 0;
}

// vi:ts=4 sw=4 expandtab
