/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sessions.c - exercise MPI-4 Sessions over Flux PMI.
 *
 * Initializes an MPI Session, builds a communicator from the WORLD process
 * set and from a proper subset (even ranks), and runs a barrier/allreduce on
 * each.  This confirms that a Session-based MPI job bootstraps and runs
 * correctly over Flux PMI.
 *
 * On MPICH >= 5.0, building the subset communicator exchanges business
 * cards among its node roots via a PMI-1.2 group barrier when the process
 * manager offers PMI-1.2, and via whole-job barriers otherwise.  See
 * t3004-mpi-sessions.t, which traces the wire protocol and asserts on it.
 *
 * Run with '--probe' to test only whether MPI-4 Sessions were compiled in
 * (returns 0) or not (returns 77 to skip); no MPI runtime is required.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if !defined(MPI_VERSION) || MPI_VERSION < 4

int main (int argc, char *argv[])
{
    fprintf (stderr, "MPI-4 Sessions not supported by this MPI\n");
    return 77; // skip
}

#else

/* The session uses MPI_ERRORS_RETURN, so every call must be checked or a
 * failure is silently swallowed.  Print the message, then the MPI error
 * string for 'rc' (which with MPICH includes an error stack), and exit.
 * The output is assembled in one buffer and written with one call so
 * that it is not interleaved with other ranks' output on unbuffered
 * stderr.
 */
static void die (int rc, const char *fmt, ...)
{
    char msg[256];
    char errstr[MPI_MAX_ERROR_STRING] = "";
    char buf[sizeof (msg) + sizeof (errstr) + 8];
    int len;
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (msg, sizeof (msg), fmt, ap);
    va_end (ap);
    if (rc != MPI_SUCCESS && MPI_Error_string (rc, errstr, &len) != MPI_SUCCESS)
        errstr[0] = '\0';
    snprintf (buf,
              sizeof (buf),
              "%s%s%s\n",
              msg,
              errstr[0] ? ": " : "",
              errstr);
    fputs (buf, stderr);
    exit (1);
}

int main (int argc, char *argv[])
{
    MPI_Session sh;
    MPI_Group world_group;
    MPI_Comm world;
    int rank, size;
    int one = 1, total = 0;
    int rc;

    if (argc > 1 && strcmp (argv[1], "--probe") == 0)
        return 0; // MPI-4 Sessions compiled in

    rc = MPI_Session_init (MPI_INFO_NULL, MPI_ERRORS_RETURN, &sh);
    if (rc != MPI_SUCCESS)
        die (rc, "MPI_Session_init failed");

    /* Full WORLD group: communicator + barrier + allreduce.
     */
    rc = MPI_Group_from_session_pset (sh, "mpi://WORLD", &world_group);
    if (rc != MPI_SUCCESS)
        die (rc, "MPI_Group_from_session_pset mpi://WORLD failed");
    rc = MPI_Comm_create_from_group (world_group,
                                     "flux.sessions.world",
                                     MPI_INFO_NULL,
                                     MPI_ERRORS_RETURN,
                                     &world);
    if (rc != MPI_SUCCESS)
        die (rc, "MPI_Comm_create_from_group WORLD failed");
    if ((rc = MPI_Comm_rank (world, &rank)) != MPI_SUCCESS)
        die (rc, "MPI_Comm_rank WORLD failed");
    if ((rc = MPI_Comm_size (world, &size)) != MPI_SUCCESS)
        die (rc, "MPI_Comm_size WORLD failed");
    if ((rc = MPI_Barrier (world)) != MPI_SUCCESS)
        die (rc, "%d: MPI_Barrier WORLD failed", rank);
    rc = MPI_Allreduce (&one, &total, 1, MPI_INT, MPI_SUM, world);
    if (rc != MPI_SUCCESS)
        die (rc, "%d: MPI_Allreduce WORLD failed", rank);
    if (total != size)
        die (MPI_SUCCESS,
             "%d: MPI_Allreduce WORLD total=%d != size=%d",
             rank,
             total,
             size);

    /* Proper subset (even ranks): only members create and barrier on the
     * subset communicator, exercising a subset group barrier.
     */
    if (size >= 2) {
        MPI_Group sub;
        MPI_Comm subcomm;
        int n = (size + 1) / 2; // count of even ranks in [0, size)
        int *ranks;
        int subrank;

        if (!(ranks = malloc (n * sizeof (int))))
            die (MPI_SUCCESS, "%d: out of memory", rank);
        for (int i = 0; i < n; i++)
            ranks[i] = 2 * i;
        if ((rc = MPI_Group_incl (world_group, n, ranks, &sub)) != MPI_SUCCESS)
            die (rc, "%d: MPI_Group_incl even failed", rank);
        free (ranks);
        if ((rc = MPI_Group_rank (sub, &subrank)) != MPI_SUCCESS)
            die (rc, "%d: MPI_Group_rank even failed", rank);
        if (subrank != MPI_UNDEFINED) {
            rc = MPI_Comm_create_from_group (sub,
                                             "flux.sessions.even",
                                             MPI_INFO_NULL,
                                             MPI_ERRORS_RETURN,
                                             &subcomm);
            if (rc != MPI_SUCCESS)
                die (rc, "%d: MPI_Comm_create_from_group even failed", rank);
            if ((rc = MPI_Barrier (subcomm)) != MPI_SUCCESS)
                die (rc, "%d: MPI_Barrier even failed", rank);
            if ((rc = MPI_Comm_free (&subcomm)) != MPI_SUCCESS)
                die (rc, "%d: MPI_Comm_free even failed", rank);
        }
        if ((rc = MPI_Group_free (&sub)) != MPI_SUCCESS)
            die (rc, "%d: MPI_Group_free even failed", rank);
    }
    if (rank == 0)
        printf ("MPI Sessions test OK: size=%d\n", size);
    if ((rc = MPI_Comm_free (&world)) != MPI_SUCCESS)
        die (rc, "%d: MPI_Comm_free WORLD failed", rank);
    if ((rc = MPI_Group_free (&world_group)) != MPI_SUCCESS)
        die (rc, "%d: MPI_Group_free WORLD failed", rank);
    if ((rc = MPI_Session_finalize (&sh)) != MPI_SUCCESS)
        die (rc, "%d: MPI_Session_finalize failed", rank);
    return 0;
}

#endif

// vi: ts=4 sw=4 expandtab
