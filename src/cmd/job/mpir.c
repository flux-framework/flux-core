/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job MPIR support */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libdebugged/debugged.h"
#include "src/shell/mpir/proctable.h"
#include "ccan/str/str.h"

#ifndef VOLATILE
# if defined(__STDC__) || defined(__cplusplus)
# define VOLATILE volatile
# else
# define VOLATILE
# endif
#endif

#define MPIR_NULL                  0
#define MPIR_DEBUG_SPAWNED         1
#define MPIR_DEBUG_ABORTING        2

VOLATILE int MPIR_debug_state    = MPIR_NULL;
struct proctable *proctable      = NULL;
MPIR_PROCDESC *MPIR_proctable    = NULL;
int MPIR_proctable_size          = 0;
char *MPIR_debug_abort_string    = NULL;
int MPIR_i_am_starter            = 1;
int MPIR_acquired_pre_main       = 1;
int MPIR_force_to_main           = 1;
int MPIR_partial_attach_ok       = 1;

static void setup_mpir_proctable (const char *s)
{
    if (!(proctable = proctable_from_json_string (s))) {
        errno = EINVAL;
        log_err_exit ("proctable_from_json_string");
    }
    MPIR_proctable = proctable_get_mpir_proctable (proctable,
                                                   &MPIR_proctable_size);
    if (!MPIR_proctable) {
        errno = EINVAL;
        log_err_exit ("proctable_get_mpir_proctable");
    }
}

static void gen_attach_signal (flux_t *h, flux_jobid_t id)
{
    flux_future_t *f = NULL;
    if (!(f = flux_job_kill (h, id, SIGCONT)))
        log_err_exit ("flux_job_kill");
    if (flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("kill %s: %s",
                      idf58 (id),
                      future_strerror (f, errno));
    flux_future_destroy (f);
}

void mpir_setup_interface (flux_t *h,
                           flux_jobid_t id,
                           bool debug_emulate,
                           bool stop_tasks_in_exec,
                           int leader_rank,
                           const char *shell_service)
{
    char topic [1024];
    const char *s = NULL;
    flux_future_t *f = NULL;

    snprintf (topic, sizeof (topic), "%s.proctable", shell_service);

    if (!(f = flux_rpc_pack (h, topic, leader_rank, 0, "{}")))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get (f, &s) < 0)
        log_err_exit ("%s", topic);

    setup_mpir_proctable (s);
    flux_future_destroy (f);

    MPIR_debug_state = MPIR_DEBUG_SPAWNED;

    /* Signal the parallel debugger */
    MPIR_Breakpoint ();

    if (stop_tasks_in_exec || debug_emulate) {
        /* To support MPIR_partial_attach_ok, we need to send SIGCONT to
         * those MPI processes to which the debugger didn't attach.
         * However, all of the debuggers that I know of do ignore
         * additional SIGCONT being sent to the processes they attached to.
         * Therefore, we send SIGCONT to *every* MPI process.
         *
         * We also send SIGCONT under the debug-emulate flag. This allows us
         * to write a test for attach mode. The running job will exit
         * on SIGCONT.
         */
        gen_attach_signal (h, id);
    }
}

/* vi: ts=4 sw=4 expandtab
 */
