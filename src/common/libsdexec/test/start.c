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

#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libsubprocess/command_private.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/jpath.h"
#include "start.h"

extern char **environ;

void test_inval (void)
{
    char *av[] = { "/bin/ls", NULL };
    int ac = 1;
    flux_t *h;
    flux_future_t *f;
    flux_cmd_t *cmd;
    json_t *cmd_o = NULL;
    json_t *cmd_o_noname;
    json_t *cmd_o_badprop;
    json_t *o;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(f = flux_future_create (NULL, 0)))
        BAIL_OUT ("could not create future for testing");
    if (!(cmd = flux_cmd_create (ac, av, environ))
        || flux_cmd_setopt (cmd, "SDEXEC_NAME", "foo") < 0
        || !(cmd_o = cmd_tojson (cmd)))
        BAIL_OUT ("could not create command object for testing");
    if (!(cmd_o_noname = json_deep_copy (cmd_o))
        || jpath_del (cmd_o_noname, "opts.SDEXEC_NAME") < 0)
        BAIL_OUT ("error preparing test command without SDEXEC_NAME");
    if (!(cmd_o_badprop = json_deep_copy (cmd_o))
        || !(o = json_string ("badvalue"))
        || jpath_set_new (cmd_o_badprop, "opts.SDEXEC_PROP_MemoryMax", o) < 0)
        BAIL_OUT ("error preparing test command with bad property");

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (NULL,      // h
                                     0,         // rank
                                     "fail",    // mode
                                     cmd_o,     // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit h=NULL fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     NULL,      // mode
                                     cmd_o,     // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit mode=NULL fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     "fail",    // mode
                                     NULL,      // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit cmd=NULL fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     "fail",    // mode
                                     cmd_o_noname, // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit missing SDEXEC_NAME fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     "fail",    // mode
                                     cmd_o_badprop, // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit with bad property fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    ok (sdexec_start_transient_unit_get (NULL, NULL) < 0 && errno == EINVAL,
        "sdexec_start_transient_unit_get f=NULL fails with EINVAL");

    json_decref (cmd_o_badprop);
    json_decref (cmd_o_noname);
    json_decref (cmd_o);
    flux_cmd_destroy (cmd);
    flux_future_destroy (f);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
