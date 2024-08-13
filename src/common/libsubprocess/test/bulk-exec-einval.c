/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libsubprocess/bulk-exec.h"
#include "ccan/str/str.h"

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    ok (bulk_exec_aux_get (NULL, NULL) == NULL && errno == EINVAL,
        "bulk_exec_aux_get (NULL, NULL) returns EINVAL");
    ok (bulk_exec_aux_set (NULL, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "bulk_exec_aux_set (NULL, ..) returns EINVAL");
    ok (bulk_exec_set_max_per_loop (NULL, 1) < 0 && errno == EINVAL,
        "bulk_exec_set_max_per_loop (NULL, 1) returns EINVAL");
    ok (bulk_exec_push_cmd (NULL, NULL, NULL, 0) < 0 && errno == EINVAL,
        "bulk_exec_push_cmd (NULL, ...) returns EINVAL");
    ok (bulk_exec_start (NULL, NULL) < 0 && errno == EINVAL,
        "bulk_exec_start (NULL, NULL) returns EINVAL");
    ok (bulk_exec_kill (NULL, NULL, 0) == NULL && errno == EINVAL,
        "bulk_exec_kill (NULL, NULL, 0) returns EINVAL");
    ok (bulk_exec_imp_kill (NULL, NULL, NULL, 0) == NULL && errno == EINVAL,
        "bulk_exec_imp_kill (NULL, NULL, NULL, 0) returns EINVAL");
    ok (bulk_exec_cancel (NULL) < 0 && errno == EINVAL,
        "bulk_exec_cancel (NULL) returns EINVAL");
    ok (bulk_exec_rc (NULL) < 0 && errno == EINVAL,
        "bulk_exec_rc (NULL) returns EINVAL");
    ok (bulk_exec_current (NULL) == 0,
        "bulk_exec_current (NULL) == 0");
    ok (bulk_exec_complete (NULL) == 0,
        "bulk_exec_complete (NULL) == 0");
    ok (bulk_exec_total (NULL) == 0,
        "bulk_exec_total (NULL) == 0");
    ok (bulk_exec_active_ranks (NULL) == NULL && errno == EINVAL,
        "bulk_exec_active_ranks (NULL) returns EINVAL");
    ok (bulk_exec_write (NULL, NULL, NULL, 0) < 0 && errno == EINVAL,
        "bulk_exec_write (NULL, ...) returns EINVAL");
    ok (bulk_exec_close (NULL, NULL) < 0 && errno == EINVAL,
        "bulk_exec_close (NULL, NULL) returns EINVAL");
    ok (bulk_exec_service_name (NULL) == NULL && errno == EINVAL,
        "bulk_exec_service_name (NULL) returns EINVAL");
    ok (bulk_exec_get_subprocess (NULL, 0) == NULL && errno == EINVAL,
        "bulk_exec_get_subprocess (NULL, 0) returns EINVAL");
    lives_ok ({bulk_exec_destroy (NULL);},
              "bulk_exec_destroy (NULL) does not die");
    lives_ok ({bulk_exec_kill_log_error (NULL, 1);},
              "bulk_exec_kill_log_error (NULL, ...) does not die");

    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
