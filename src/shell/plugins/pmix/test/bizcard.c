/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
 \************************************************************/

/* bizcard.c - procs exchange "business cards" (spec v5.0 sec B.1) */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <pmix.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"

int main (int argc, char **argv)
{
    char name[512];
    char buf[2048];
    pmix_proc_t self;
    pmix_proc_t proc;
    pmix_value_t val;
    pmix_value_t *valp;
    pmix_info_t info;
    struct timespec t;
    int rc;
    int size;
    char hostname[128];
    int lrank;
    int srank;

    /* Initialize and set log prefix to nspace.rank
     */
    if ((rc = PMIx_Init (&self, NULL, 0)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Init: %s", PMIx_Error_string (rc));
    snprintf (name, sizeof (name), "%s.%d", self.nspace, self.rank);
    log_init (name);
    if (self.rank == 0)
        log_msg ("completed PMIx_Init.");

    /* Get the size
     */
    strncpy (proc.nspace, self.nspace, PMIX_MAX_NSLEN);
    proc.nspace[PMIX_MAX_NSLEN] = '\0';
    proc.rank = PMIX_RANK_WILDCARD;
    if ((rc = PMIx_Get (&proc,
                        PMIX_JOB_SIZE,
                        NULL,
                        0,
                        &valp)) != PMIX_SUCCESS) {
        log_msg_exit ("PMIx_Get %s: %s",
                      PMIX_JOB_SIZE,
                      PMIx_Error_string (rc));
    }
    if (valp->type != PMIX_UINT32)
        log_msg_exit ("PMIx_Get %s: return unexpected type", PMIX_JOB_SIZE);
    size = valp->data.uint32;
    if (self.rank == 0)
        log_msg ("there are %d tasks", size);
    PMIX_VALUE_RELEASE (valp);

    /* Get the hostname
     */
    if ((rc = PMIx_Get (&self, PMIX_HOSTNAME, NULL, 0, &valp)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Get %s: %s", PMIX_HOSTNAME, PMIx_Error_string (rc));
    if (valp->type != PMIX_STRING)
        log_msg_exit ("PMIx_Get %s: return unexpected type", PMIX_HOSTNAME);
    snprintf (hostname, sizeof (hostname), "%s", valp->data.string);
    PMIX_VALUE_RELEASE (valp);

    /* Get the local rank
     */
    if ((rc = PMIx_Get (&self,
                        PMIX_LOCAL_RANK,
                        NULL,
                        0,
                        &valp)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Get %s: %s",
                      PMIX_LOCAL_RANK,
                      PMIx_Error_string (rc));
    if (valp->type != PMIX_UINT16)
        log_msg_exit ("PMIx_Get %s: return unexpected type", PMIX_LOCAL_RANK);
    lrank = valp->data.uint16;
    PMIX_VALUE_RELEASE (valp);

    /* Get the server rank
     */
    if ((rc = PMIx_Get (&self,
                        PMIX_SERVER_RANK,
                        NULL,
                        0,
                        &valp)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Get %s: %s",
                      PMIX_SERVER_RANK,
                      PMIx_Error_string (rc));
    if (valp->type != PMIX_PROC_RANK)
        log_msg_exit ("PMIx_Get %s: return unexpected type", PMIX_SERVER_RANK);
    srank = valp->data.rank;
    PMIX_VALUE_RELEASE (valp);


    /* Store the business card for our nspace, rank.
     */
    snprintf (buf,
              sizeof (buf),
              "+-------------------------------\n"
              "| Hello, my name is %s.%d\n"
              "|   I live on %s\n"
              "|   My local rank is %d\n"
              "|   My server rank is %d\n"
              "+-------------------------------\n",
              self.nspace, self.rank,
              hostname,
              lrank,
              srank);
    val.type = PMIX_STRING;
    val.data.string = buf;
    if ((rc = PMIx_Put (PMIX_GLOBAL, "card", &val)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Put %s: %s", "card", PMIx_Error_string (rc));
    if ((rc = PMIx_Commit ()) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Commit: %s", PMIx_Error_string (rc));

    /* Fence
     */
    strncpy (info.key, PMIX_COLLECT_DATA, PMIX_MAX_KEYLEN);
    info.key[PMIX_MAX_KEYLEN] = '\0';
    info.value.type = PMIX_BOOL;
    info.value.data.flag = true;

    monotime (&t);
    if ((rc = PMIx_Fence (NULL, 0, &info, 1)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Fence: %s", PMIx_Error_string (rc));
    if (self.rank == 0)
        log_msg ("PMIx_Fence completed in %0.3fs", monotime_since (t) / 1000);

    /* Fetch specified card(s) and print.
     */
    if (self.rank == 0 && argc > 1) {
        for (int optindex = 1; optindex < argc; optindex++) {
            pmix_proc_t proc;
            pmix_value_t *valp;

            strncpy (proc.nspace, self.nspace, PMIX_MAX_NSLEN);
            proc.nspace[PMIX_MAX_NSLEN] = '\0';
            errno = 0;
            proc.rank = strtoul (argv[optindex], NULL, 10);
            if (errno != 0)
                log_err_exit ("Error parsing argument '%s'", argv[optindex]);

            if ((rc = PMIx_Get (&proc,
                                "card",
                                NULL,
                                0,
                                &valp)) != PMIX_SUCCESS) {
                log_msg_exit ("PMIx_Get rank %d card: %s",
                              proc.rank,
                              PMIx_Error_string (rc));
            }
            if (valp->type != PMIX_STRING) {
                log_msg_exit ("PMIx_Get rank %d card: returned wrong type",
                              proc.rank);
            }
            fprintf (stderr, "%s", valp->data.string);
            PMIX_VALUE_RELEASE (valp);
        }
    }

    /* Finalize
     */
    if ((rc = PMIx_Finalize (NULL, 0)) != PMIX_SUCCESS)
        log_msg_exit ("PMIx_Finalize: %s", PMIx_Error_string (rc));
    if (self.rank == 0)
        log_msg ("completed PMIx_Finalize");

    return 0;
}

// vi:ts=4 sw=4 expandtab
