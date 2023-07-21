/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <linux/limits.h>


#include "src/common/libtap/tap.h"
#include "src/common/libsdprocess/sdprocess.h"
#include "src/common/libsdprocess/sdprocess_private.h"
#include "src/common/libsdprocess/strv.h"
#include "src/common/libutil/fluid.h"
#include "ccan/str/str.h"
#include "src/common/libtestutil/util.h"

static struct fluid_generator gen;
static bool fluid_generator_init = false;

char true_cmd[PATH_MAX];
char false_cmd[PATH_MAX];
char sleep_cmd[PATH_MAX];
char *true_cmdv[] = {true_cmd, NULL};
char *false_cmdv[] = {false_cmd, NULL};
char *sleep30_cmdv[] = {sleep_cmd, "30", NULL};

struct state_counts {
    int active_count;
    int exited_count;
};

// NOTE: sourced from ssh connector
static char *which (const char *prog, char *buf, size_t size)
{
    char *path = getenv ("PATH");
    char *cpy = path ? strdup (path) : NULL;
    char *dir, *saveptr = NULL, *a1 = cpy;
    struct stat sb;
    char *result = NULL;

    if (cpy) {
        while ((dir = strtok_r (a1, ":", &saveptr))) {
            snprintf (buf, size, "%s/%s", dir, prog);
            if (stat (buf, &sb) == 0 && S_ISREG (sb.st_mode)
                                     && access (buf, X_OK) == 0) {
                result = buf;
                break;
            }
            a1 = NULL;
        }
    }
    free (cpy);
    return result;
}

static char *get_unitname (const char *prefix)
{
    fluid_t fluid;
    char *unitname;

    if (!fluid_generator_init) {
        /* Consideration: use constant timestamp so unit names are
         * consistent in tests? */
        if (fluid_init (&gen, 0, time (NULL)) < 0)
            BAIL_OUT ("fluid_init");
        fluid_generator_init = true;
    }

    if (fluid_generate (&gen, &fluid) < 0)
        BAIL_OUT ("fluid_generate");

    if (asprintf (&unitname,
                  "%s-%ju",
                  prefix ? prefix : "libsdprocess-test",
                  (uintmax_t) fluid) < 0)
        BAIL_OUT ("asprintf");

    return unitname;
}

static void sdprocess_systemd_cleanup_wrap (sdprocess_t *sdp)
{
    int ret = sdprocess_systemd_cleanup (sdp);
    while (ret < 0 && errno == EBUSY) {
        usleep (100000);
        ret = sdprocess_systemd_cleanup (sdp);
    }
    ok (ret == 0,
        "sdprocess_systemd_cleanup success");
}

static void sdprocess_kill_wrap (sdprocess_t *sdp, int signo)
{
    int ret = sdprocess_kill (sdp, signo);
    while (ret < 0 && errno == EPERM) {
        usleep (100000);
        ret = sdprocess_kill (sdp, signo);
    }
    ok (ret == 0,
        "sdprocess_kill success");
}

static void test_corner_case (void)
{
    sdprocess_t *sdp;
    const char *unitname;
    bool active, exited;
    int ret;

    sdp = sdprocess_exec (NULL, NULL, NULL, NULL, -1, -1, -1);
    ok (sdp == NULL && errno == EINVAL,
        "sdprocess_exec returns EINVAL on invalid input");

    sdp = sdprocess_find_unit (NULL, NULL);
    ok (sdp == NULL && errno == EINVAL,
        "sdprocess_find_unit returns EINVAL on invalid input");

    unitname = sdprocess_unitname (sdp);
    ok (unitname == NULL && errno == EINVAL,
        "sdprocess_unitname returns EINVAL on invalid input");

    ret = sdprocess_pid (sdp);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_pid returns EINVAL on invalid input");

    ret = sdprocess_state (NULL, NULL, NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_state returns EINVAL on invalid input");

    ret = sdprocess_wait (NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_wait returns EINVAL on invalid input");

    active = sdprocess_active (NULL);
    ok (active == false,
        "sdprocess_active returns false on invalid input");

    exited = sdprocess_exited (NULL);
    ok (exited == false,
        "sdprocess_exited returns false on invalid input");

    ret = sdprocess_exit_status (NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_exit_status returns EINVAL on invalid input");

    ret = sdprocess_wait_status (NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_wait_status returns EINVAL on invalid input");

    ret = sdprocess_kill (NULL, -1);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_kill returns EINVAL on invalid input");

    ret = sdprocess_systemd_cleanup (NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_systemd_cleanup returns EINVAL on invalid input");

    ret = sdprocess_list (NULL, NULL, NULL, NULL);
    ok (ret < 0 && errno == EINVAL,
        "sdprocess_list returns EINVAL on invalid input");

    /* can pass NULL to destroy */
    sdprocess_destroy (NULL);
}

static void test_basic (flux_t *h,
                        char **cmdv,
                        int expected_exit_status)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_exit_status (sdp);
    printf("got: %d\n", ret);
    char **cmdv2 = cmdv;
    while(*cmdv2) {
        printf("cmd: %s\n", *cmdv2++);
    }
    ok (ret == expected_exit_status,
        "sdprocess_exit_status returns correct exit status");

    ret = sdprocess_wait_status (sdp);
    ok (WIFEXITED (ret) && WEXITSTATUS (ret) == expected_exit_status,
        "sdprocess_wait_status returns correct wait status");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_success (flux_t *h)
{
    test_basic (h, true_cmdv, 0);
}

static void test_failure (flux_t *h)
{
    test_basic (h, false_cmdv, 1);
}

static void test_unitname (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    const char *unitnameptr;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    unitnameptr = sdprocess_unitname (sdp);
    ok (unitnameptr && streq (unitname, unitnameptr),
        "sdprocess_unitname returns correct unitname");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_pid (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool active;
    int pid;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    /* just make sure its an ok looking pid */
    pid = sdprocess_pid (sdp);
    ok (pid > 0,
        "sdprocess_pid returned legit looking pid: %d", pid);

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    pid = sdprocess_pid (sdp);
    ok (pid < 0 && errno == EPERM,
        "sdprocess_pid EPERM on non-running process");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_duplicate (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    sdprocess_t *sdp_dup = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdp_dup = sdprocess_exec (h,
                              unitname,
                              sleep30_cmdv,
                              NULL,
                              STDIN_FILENO,
                              STDOUT_FILENO,
                              STDERR_FILENO);
    ok (sdp_dup == NULL && errno == EEXIST,
        "sdprocess_exec returns EEXIST on duplicate unitname");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_active (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_exited (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool exited;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    exited = sdprocess_exited (sdp);
    ok (exited == false,
        "sdprocess_exited returns false before its done");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    exited = sdprocess_exited (sdp);
    ok (exited == true,
        "sdprocess_exited returns true after process exits");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_exit_status (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_exit_status (sdp);
    ok (ret < 0 && errno == EBUSY,
        "sdprocess_exit_status returns EBUSY before its done");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_exit_status (sdp);
    ok (ret == SIGKILL,
        "sdprocess_exit_status returns SIGKILL after its done");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_wait_status (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait_status (sdp);
    ok (ret < 0 && errno == EBUSY,
        "sdprocess_wait_status returns EBUSY before its done");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_wait_status (sdp);
    ok (WIFSIGNALED (ret) && WTERMSIG (ret) == SIGKILL,
        "sdprocess_wait_status returns correct signal after its done");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_wait (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_wait_after_exited (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait on already exited process success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void count_and_kill_cb (sdprocess_t *sdp,
                               sdprocess_state_t state,
                               void *arg)
{
    struct state_counts *counts = arg;
    if (state == SDPROCESS_ACTIVE) {
        sdprocess_kill_wrap (sdp, SIGKILL);
        counts->active_count++;
    }
    if (state == SDPROCESS_EXITED)
        counts->exited_count++;
}

static void test_state (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    struct state_counts counts = {0};
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_state (sdp, count_and_kill_cb, &counts);
    ok (ret == 0,
        "sdprocess_state success");

    ret = flux_reactor_run (flux_get_reactor (h), 0);
    ok (ret == 0,
        "flux_reactor_run success");

    ok (counts.active_count == 1,
        "active state callback once");

    ok (counts.exited_count == 1,
        "exit state callback once");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void count_cb (sdprocess_t *sdp,
                      sdprocess_state_t state,
                      void *arg)
{
    struct state_counts *counts = arg;
    if (state == SDPROCESS_ACTIVE)
        counts->active_count++;
    if (state == SDPROCESS_EXITED)
        counts->exited_count++;
}

static void test_state_after_exited (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    struct state_counts counts = {0};
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_state (sdp, count_cb, &counts);
    ok (ret == 0,
        "sdprocess_state success");

    ret = flux_reactor_run (flux_get_reactor (h), 0);
    ok (ret == 0,
        "flux_reactor_run success");

    ok (counts.active_count == 0,
        "active state callback never called, process exited before we started");

    ok (counts.exited_count == 1,
        "exit state callback once");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_kill (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdprocess_kill_wrap (sdp, SIGUSR1);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_exit_status (sdp);
    ok (ret == SIGUSR1,
        "sdprocess_exit_status returns correct exit status");

    ret = sdprocess_wait_status (sdp);
    ok (WIFSIGNALED (ret) && WTERMSIG (ret) == SIGUSR1,
        "sdprocess_exit_status returns correct wait status");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_kill_after_exited_success (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_kill (sdp, SIGUSR1);
    ok (ret == 0,
        "sdprocess_kill success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_kill_after_exited_failure (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          false_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_kill (sdp, SIGUSR1);
    ok (ret < 0 && errno == EPERM,
        "sdprocess_kill EPERM can't send signal");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_find_unit (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    sdprocess_t *sdpfind = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    /* avoid test raciness */
    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdpfind = sdprocess_find_unit (h, unitname);
    ok (sdpfind != NULL,
        "sdprocess_find_unit found process");

    sdprocess_kill_wrap (sdpfind, SIGKILL);

    ret = sdprocess_wait (sdpfind);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdpfind);

    free (unitname);
    sdprocess_destroy (sdp);
    sdprocess_destroy (sdpfind);
}

static void test_find_unit_not_exist (flux_t *h)
{
    sdprocess_t *sdpfind = NULL;

    sdpfind = sdprocess_find_unit (h, "foobar");
    ok (sdpfind == NULL && errno == ENOENT,
        "sdprocess_find_unit returned ENOENT for bad unitname");
}

static void test_find_unit_state (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    sdprocess_t *sdpfind = NULL;
    struct state_counts counts = {0};
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    /* avoid test raciness */
    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdpfind = sdprocess_find_unit (h, unitname);
    ok (sdpfind != NULL,
        "sdprocess_find_unit found process");

    ret = sdprocess_state (sdpfind, count_and_kill_cb, &counts);
    ok (ret == 0,
        "sdprocess_state success");

    ret = flux_reactor_run (flux_get_reactor (h), 0);
    ok (ret == 0,
        "flux_reactor_run success");

    ok (counts.active_count == 1,
        "active state callback once");

    ok (counts.exited_count == 1,
        "exit state callback once");

    sdprocess_systemd_cleanup_wrap (sdpfind);

    free (unitname);
    sdprocess_destroy (sdp);
    sdprocess_destroy (sdpfind);
}

static void test_find_unit_after_exited (flux_t *h,
                                         char *cmdv[],
                                         int expected_exit_status)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    sdprocess_t *sdpfind = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    /* avoid small potential test raciness */
    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdpfind = sdprocess_find_unit (h, unitname);
    ok (sdpfind != NULL,
        "sdprocess_find_unit found process");

    ret = sdprocess_wait (sdpfind);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_exit_status (sdpfind);
    ok (ret == expected_exit_status,
        "sdprocess_exit_status returns correct exit status");

    ret = sdprocess_wait_status (sdpfind);
    ok (WIFEXITED (ret) && WEXITSTATUS (ret) == expected_exit_status,
        "sdprocess_wait_status returns correct wait status");

    sdprocess_systemd_cleanup_wrap (sdpfind);

    free (unitname);
    sdprocess_destroy (sdp);
    sdprocess_destroy (sdpfind);
}

static void test_find_unit_after_exited_success (flux_t *h)
{
    test_find_unit_after_exited (h, true_cmdv, 0);
}

static void test_find_unit_after_exited_failure (flux_t *h)
{
    test_find_unit_after_exited (h, false_cmdv, 1);
}

static void test_find_unit_after_signaled (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    sdprocess_t *sdpfind = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    /* avoid test raciness */
    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    sdpfind = sdprocess_find_unit (h, unitname);
    ok (sdpfind != NULL,
        "sdprocess_find_unit found process");

    sdprocess_kill_wrap (sdpfind, SIGKILL);

    ret = sdprocess_wait (sdpfind);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_exit_status (sdpfind);
    ok (ret == SIGKILL,
        "sdprocess_exit_status returns correct exit status");

    ret = sdprocess_wait_status (sdpfind);
    ok (WIFSIGNALED (ret) && WTERMSIG (ret) == SIGKILL,
        "sdprocess_wait_status returns correct signal after its done");

    sdprocess_systemd_cleanup_wrap (sdpfind);

    free (unitname);
    sdprocess_destroy (sdp);
    sdprocess_destroy (sdpfind);
}

static char **test_cmd_cmdv (char *cmdline)
{
    char *cwd = get_current_dir_name ();
    char *fullcmd = NULL;
    char **cmdv;

    /* cmd must be absolute for now */
    if (asprintf (&fullcmd,
                  "%s/%s/%s",
                  cwd,
                  TEST_SDPROCESS_DIR,
                  cmdline) < 0)
        BAIL_OUT ("asprintf");

    cmdv = strv_create (fullcmd, " ");

    free (fullcmd);
    free (cwd);

    return cmdv;
}

static void test_output (flux_t *h,
                         bool do_stdout)
{
    char *unitname = get_unitname (NULL);
    char **cmdv = NULL;
    sdprocess_t *sdp = NULL;
    char buf[1024] = {0};
    int index = 0;
    int fds[2];
    int ret;

    if (do_stdout)
        cmdv = test_cmd_cmdv ("test_echo -O foobar");
    else
        cmdv = test_cmd_cmdv ("test_echo -E foobar");

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fds) < 0)
        BAIL_OUT ("socketpair");

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          -1,
                          do_stdout ? fds[1] : -1,
                          do_stdout ? -1 : fds[1]);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    while ((ret = read (fds[0], buf + index, 1024 - index)) > 0) {
        /* 7 = strlen ("foobar") + 1 for newline */
        index += ret;
        if (index >= 7)
            break;
    }
    if (ret < 0)
        BAIL_OUT ("read");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ok (streq (buf, "foobar\n"),
        "%s received the right output", do_stdout ? "stdout" : "stderr");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    close (fds[0]);
    close (fds[1]);
    strv_destroy (cmdv);
    sdprocess_destroy (sdp);
}

static void test_stdout (flux_t *h)
{
    test_output (h, true);
}

static void test_stderr (flux_t *h)
{
    test_output (h, false);
}

static void test_stdin (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    char **cmdv = test_cmd_cmdv ("test_echo -O");
    sdprocess_t *sdp = NULL;
    bool active;
    char buf[1024] = {0};
    int index = 0;
    int stdin_fds[2];
    int stdout_fds[2];
    int ret;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, stdin_fds) < 0)
        BAIL_OUT ("socketpair");
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, stdout_fds) < 0)
        BAIL_OUT ("socketpair");

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          stdin_fds[1],
                          stdout_fds[1],
                          -1);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    if (write (stdin_fds[0], "foobar", 6) < 0)
        BAIL_OUT ("write");
    close (stdin_fds[0]);

    while ((ret = read (stdout_fds[0], buf + index, 1024 - index)) > 0) {
        /* 7 = strlen ("foobar") + 1 for newline */
        index += ret;
        if (index >= 7)
            break;
    }
    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ok (streq (buf, "foobar\n"),
        "stdout received the right output");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    close (stdin_fds[0]);
    close (stdin_fds[1]);
    close (stdout_fds[0]);
    close (stdout_fds[1]);
    strv_destroy (cmdv);
    sdprocess_destroy (sdp);
}

static void test_environment (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    char **cmdv = test_cmd_cmdv ("test_env FOO");
    char *env[] = { "FOO=BAR", NULL };
    sdprocess_t *sdp = NULL;
    char buf[1024] = {0};
    int index = 0;
    int fds[2];
    int ret;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fds) < 0)
        BAIL_OUT ("socketpair");

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          env,
                          -1,
                          fds[1],
                          -1);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    while ((ret = read (fds[0], buf + index, 1024 - index)) > 0) {
        /* 8 = strlen ("FOO=BAR") + 1 for newline */
        index += ret;
        if (index >= 8)
            break;
    }
    if (ret < 0)
        BAIL_OUT ("read");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ok (streq (buf, "FOO=BAR\n"),
        "stdout received message indicating environment set correctly");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    close (fds[0]);
    close (fds[1]);
    strv_destroy (cmdv);
    sdprocess_destroy (sdp);
}

static void test_no_such_command (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    char *cmdv[] = { "/bin/nosuchcommand", NULL };
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    /* 203 special exec error status for systemd */
    ret = sdprocess_exit_status (sdp);
    ok (ret == 203,
        "sdprocess_exit_status returns correct exit code for exec error");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static int list_count_cb (flux_t *h,
                          const char *unitname,
                          void *arg)
{
    int *count = arg;
    (*count)++;
    return 0;
}

static void test_list (flux_t *h)
{
    char *unitname1 = get_unitname ("libsdprocess-test-listA");
    char *unitname2 = get_unitname ("libsdprocess-test-listB");
    sdprocess_t *sdp1 = NULL;
    sdprocess_t *sdp2 = NULL;
    int ret, count;

    sdp1 = sdprocess_exec (h,
                           unitname1,
                           true_cmdv,
                           NULL,
                           STDIN_FILENO,
                           STDOUT_FILENO,
                           STDERR_FILENO);
    ok (sdp1 != NULL,
        "sdprocess_exec launched process under systemd");

    sdp2 = sdprocess_exec (h,
                           unitname2,
                           true_cmdv,
                           NULL,
                           STDIN_FILENO,
                           STDOUT_FILENO,
                           STDERR_FILENO);
    ok (sdp2 != NULL,
        "sdprocess_exec launched process under systemd");

    count = 0;
    ret = sdprocess_list (NULL, NULL, list_count_cb, &count);
    ok (ret == 0,
        "sdprocess_list success");

    ok (count >= 2,
        "sdprocess_list listed all units");

    count = 0;
    ret = sdprocess_list (NULL,
                          "libsdprocess-test-list*",
                          list_count_cb,
                          &count);
    ok (ret == 0,
        "sdprocess_list success");

    ok (count == 2,
        "sdprocess_list listed libsdprocess-test-list* units");

    count = 0;
    ret = sdprocess_list (NULL,
                          "libsdprocess-test-listA*",
                          list_count_cb,
                          &count);
    ok (ret == 0,
        "sdprocess_list success");

    ok (count == 1,
        "sdprocess_list listed libsdprocess-test-listA units");

    ret = sdprocess_wait (sdp1);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_wait (sdp2);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp1);
    sdprocess_systemd_cleanup_wrap (sdp2);

    free (unitname1);
    free (unitname2);
    sdprocess_destroy (sdp1);
    sdprocess_destroy (sdp2);
}

static int list_error_cb (flux_t *h,
                          const char *unitname,
                          void *arg)
{
    /* picking a weird errno for test */
    errno = EXDEV;
    return -1;
}

static void test_list_error (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_list (NULL, NULL, list_error_cb, NULL);
    ok (ret < 0 && errno == EXDEV,
        "sdprocess_list errors out with EXDEV on callback error");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static int list_early_exit_cb (flux_t *h,
                               const char *unitname,
                               void *arg)
{
    int *count = arg;
    if ((*count) == 0) {
        (*count)++;
        return 1;
    }
    return 0;
}

static void test_list_early_exit (flux_t *h)
{
    char *unitname1 = get_unitname ("libsdprocess-test-listA");
    char *unitname2 = get_unitname ("libsdprocess-test-listB");
    sdprocess_t *sdp1 = NULL;
    sdprocess_t *sdp2 = NULL;
    int ret, count;

    sdp1 = sdprocess_exec (h,
                           unitname1,
                           true_cmdv,
                           NULL,
                           STDIN_FILENO,
                           STDOUT_FILENO,
                           STDERR_FILENO);
    ok (sdp1 != NULL,
        "sdprocess_exec launched process under systemd");

    sdp2 = sdprocess_exec (h,
                           unitname2,
                           true_cmdv,
                           NULL,
                           STDIN_FILENO,
                           STDOUT_FILENO,
                           STDERR_FILENO);
    ok (sdp2 != NULL,
        "sdprocess_exec launched process under systemd");

    count = 0;
    ret = sdprocess_list (NULL, NULL, list_early_exit_cb, &count);
    ok (ret == 0,
        "sdprocess_list success");

    ok (count == 1,
        "sdprocess_list callback called only once");

    ret = sdprocess_wait (sdp1);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_wait (sdp2);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp1);
    sdprocess_systemd_cleanup_wrap (sdp2);

    free (unitname1);
    free (unitname2);
    sdprocess_destroy (sdp1);
    sdprocess_destroy (sdp2);
}

static void test_invalid_permissions_command (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    char *cmdv[] = { "/bin", NULL };
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    /* 203 special exec error status for systemd */
    ret = sdprocess_exit_status (sdp);
    ok (ret == 203,
        "sdprocess_exit_status returns correct exit code for exec error");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static bool unit_listed (char *unitname)
{
    int ret, count = 0;
    char *pattern = NULL;

    if (asprintf (&pattern,
                  "%s*",
                  unitname) < 0)
        BAIL_OUT ("asprintf");

    ret = sdprocess_list (NULL, pattern, list_count_cb, &count);
    ok (ret == 0,
        "sdprocess_list success");

    free (pattern);
    return count > 0 ? true : false;
}

static void test_cleanup_success (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool listed;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    listed = unit_listed (unitname);
    ok (listed == true,
        "before cleanup unit is listed");

    ret = sdprocess_systemd_cleanup (sdp);
    ok (ret == 0,
        "sdprocess_systemd_cleanup success");

    sdprocess_destroy (sdp);

    listed = unit_listed (unitname);
    while (listed == true) {
        usleep (100000);
        listed = unit_listed (unitname);
    }
    ok (listed == false,
        "after cleanup unit is no longer listed");

    free (unitname);
}

static void test_cleanup_failure (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool listed;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          false_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    listed = unit_listed (unitname);
    ok (listed == true,
        "before cleanup unit is listed");

    ret = sdprocess_systemd_cleanup (sdp);
    ok (ret == 0,
        "sdprocess_systemd_cleanup success");

    sdprocess_destroy (sdp);

    listed = unit_listed (unitname);
    while (listed == true) {
        usleep (100000);
        listed = unit_listed (unitname);
    }
    ok (listed == false,
        "after cleanup unit is no longer listed");

    free (unitname);
}

static void test_cleanup_before_exited (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    bool active;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          sleep30_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    active = sdprocess_active (sdp);
    while (!active) {
        usleep (100000);
        active = sdprocess_active (sdp);
    }
    ok (active == true,
        "sdprocess_active success");

    ret = sdprocess_systemd_cleanup (sdp);
    ok (ret < 0 && errno == EBUSY,
        "sdprocess_systemd_cleanup EBUSY with still running unit");

    sdprocess_kill_wrap (sdp, SIGKILL);

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    sdprocess_systemd_cleanup_wrap (sdp);

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_cleanup_success_after_cleanup (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          true_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_systemd_cleanup (sdp);
    ok (ret == 0,
        "sdprocess_systemd_cleanup success");

    /* after first cleanup, could take awhile to reach final state */
    ret = sdprocess_systemd_cleanup (sdp);
    while (!ret || errno != EPERM) {
        usleep (100000);
        ret = sdprocess_systemd_cleanup (sdp);
    }
    ok (ret < 0 && errno == EPERM,
        "sdprocess_systemd_cleanup EPERM with already cleaned up unit");

    free (unitname);
    sdprocess_destroy (sdp);
}

static void test_cleanup_failure_after_cleanup (flux_t *h)
{
    char *unitname = get_unitname (NULL);
    sdprocess_t *sdp = NULL;
    int ret;

    sdp = sdprocess_exec (h,
                          unitname,
                          false_cmdv,
                          NULL,
                          STDIN_FILENO,
                          STDOUT_FILENO,
                          STDERR_FILENO);
    ok (sdp != NULL,
        "sdprocess_exec launched process under systemd");

    ret = sdprocess_wait (sdp);
    ok (ret == 0,
        "sdprocess_wait success");

    ret = sdprocess_systemd_cleanup (sdp);
    ok (ret == 0,
        "sdprocess_systemd_cleanup success");

    /* after first cleanup, could take awhile to reach final state */
    ret = sdprocess_systemd_cleanup (sdp);
    while (!ret || errno != EPERM) {
        usleep (100000);
        ret = sdprocess_systemd_cleanup (sdp);
    }
    ok (ret < 0 && errno == EPERM,
        "sdprocess_systemd_cleanup EPERM with already cleaned up unit");

    free (unitname);
    sdprocess_destroy (sdp);
}

int main (int argc, char *argv[])
{
    flux_t *h = loopback_create (0);
    const char *dbusenv;
    const char *xdgenv;

    plan (NO_PLAN);

    if (!h)
        BAIL_OUT ("unable to create test handle");

    if (!(dbusenv = getenv ("DBUS_SESSION_BUS_ADDRESS"))
        || !(xdgenv = getenv ("XDG_RUNTIME_DIR"))) {
        diag ("DBUS_SESSION_BUS_ADDRESS or XDG_RUNTIME_DIR not set");
        done_testing ();
    }

    /* Instead of checking if user service running via some equivalent to
     *
     * systemctl list-units user@UID.service
     *
     * just make sure the path to XDG_RUNTIME_DIR exists
     *
     * we don't check DBUS_SESSION_BUS_ADDRESS since it often has
     * funky formatting (e.g. unix:path=/run/user/8556/bus)
     */
    if (access (xdgenv, R_OK | W_OK) < 0) {
        diag ("cannot access XDG_RUNTIME_DIR");
        done_testing ();
    }

    /* initialize paths to true, false and sleep in case they are not in /bin
     */
    if (which("true", true_cmd, PATH_MAX) == NULL
     || which("false", false_cmd, PATH_MAX) == NULL
     || which("sleep", sleep_cmd, PATH_MAX) == NULL) {
        diag ("necessary test binary missing, one of true, false or sleep");
        done_testing ();
    }

    /* loopback handle can't handle flux_log(), so disable it for
     * these tests */
    sdprocess_logging (false);

    diag ("corner_case");
    test_corner_case ();
    diag ("success");
    test_success (h);
    diag ("failure");
    test_failure (h);
    diag ("unitname");
    test_unitname (h);
    diag ("pid");
    test_pid (h);
    diag ("duplicate");
    test_duplicate (h);
    diag ("active");
    test_active (h);
    diag ("exited");
    test_exited (h);
    diag ("exit_status");
    test_exit_status (h);
    diag ("wait_status");
    test_wait_status (h);
    diag ("wait");
    test_wait (h);
    diag ("wait_after_exited");
    test_wait_after_exited (h);
    diag ("state");
    test_state (h);
    diag ("state_after_exited");
    test_state_after_exited (h);
    diag ("kill");
    test_kill (h);
    diag ("kill_after_exited_success");
    test_kill_after_exited_success (h);
    diag ("kill_after_exited_failure");
    test_kill_after_exited_failure (h);
    diag ("find_unit");
    test_find_unit (h);
    diag ("find_unit_not_exist");
    test_find_unit_not_exist (h);
    diag ("find_unit_state");
    test_find_unit_state (h);
    diag ("find_unit_after_exited_success");
    test_find_unit_after_exited_success (h);
    diag ("find_unit_after_exited_failure");
    test_find_unit_after_exited_failure (h);
    diag ("find_unit_after_signaled");
    test_find_unit_after_signaled (h);
    diag ("stdout");
    test_stdout (h);
    diag ("stderr");
    test_stderr (h);
    diag ("stdin");
    test_stdin (h);
    diag ("environment");
    test_environment (h);
    diag ("list");
    test_list (h);
    diag ("list_error");
    test_list_error (h);
    diag ("list_early_exit");
    test_list_early_exit (h);
    diag ("no_such_command");
    test_no_such_command (h);
    diag ("invalid_permissions_command");
    test_invalid_permissions_command (h);
    diag ("cleanup_success");
    test_cleanup_success (h);
    diag ("cleanup_failure");
    test_cleanup_failure (h);
    diag ("cleanup_before_exited");
    test_cleanup_before_exited (h);
    diag ("cleanup_success_after_cleanup");
    test_cleanup_success_after_cleanup (h);
    diag ("cleanup_failure_after_cleanup");
    test_cleanup_failure_after_cleanup (h);

    flux_close (h);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
