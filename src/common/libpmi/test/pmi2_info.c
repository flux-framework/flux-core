/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi/pmi2.h"
#include "src/common/libpmi/pmi_strerror.h"

/* We don't have a pmi2_strerror() but the codes are mostly the same as PMI-1
 */
#define pmi2_strerror pmi_strerror

#define OPTIONS "a:"
static const struct option longopts[] = {
    {"abort",        required_argument,  0, 'a'},
    {0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
    int spawned, size, rank, appnum;
    char jobid[PMI2_MAX_VALLEN];
    char map[PMI2_MAX_ATTRVALUE];
    char usize[PMI2_MAX_ATTRVALUE];
    int e;
    int ch;
    int abort_rank = -1;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'a':   /* --abort */
                abort_rank = atoi (optarg);
                break;
        }
    }
    e = PMI2_Init (&spawned, &size, &rank, &appnum);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("PMI2_Init: %s", pmi2_strerror (e));
    if (PMI2_Initialized () == 0)
        log_msg_exit ("%d: PMI2_Initialized returned 0", rank);

    e = PMI2_Job_GetId (jobid, sizeof (jobid));
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Job_Getid: %s", rank, pmi2_strerror (e));

    e = PMI2_Info_GetJobAttr ("PMI_process_mapping", map, sizeof (map), NULL);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetJobAttr PMI_process_mapping: %s",
                      rank, pmi2_strerror (e));

    e = PMI2_Info_GetJobAttr ("universeSize", usize, sizeof (usize), NULL);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetJobAttr universeSize: %s",
                      rank, pmi2_strerror (e));

    printf ("%d: size=%d appnum=%d jobid=%s"
            " PMI_process_mapping=%s universeSize=%s\n",
            rank, size, appnum, jobid, map, usize);

    if (abort_rank == rank)
        PMI2_Abort (1, "This is a PMI2_Abort message.\nWith\nMultiple\nLines");

    e = PMI2_Finalize ();
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Finalize: %s", rank, pmi2_strerror (e));

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
