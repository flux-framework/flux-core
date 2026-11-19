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
#include "ccan/str/str.h"
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

    /* bad AllowedMemoryNodes: invalid idset */
    json_t *cmd_o_badnodes;
    if (!(cmd_o_badnodes = json_deep_copy (cmd_o))
        || !(o = json_string ("badvalue"))
        || jpath_set_new (cmd_o_badnodes,
                          "opts.SDEXEC_PROP_AllowedMemoryNodes", o) < 0)
        BAIL_OUT ("error preparing test command with bad AllowedMemoryNodes");

    /* bad DeviceAllow: missing perms field */
    json_t *cmd_o_baddevice;
    if (!(cmd_o_baddevice = json_deep_copy (cmd_o))
        || !(o = json_string ("/dev/nvidiactl"))
        || jpath_set_new (cmd_o_baddevice, "opts.SDEXEC_PROP_DeviceAllow", o) < 0)
        BAIL_OUT ("error preparing test command with bad DeviceAllow");

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
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     "fail",    // mode
                                     cmd_o_baddevice, // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit with bad DeviceAllow fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    error.text[0] = '\0';
    ok (sdexec_start_transient_unit (h,         // h
                                     0,         // rank
                                     "fail",    // mode
                                     cmd_o_badnodes, // cmd
                                     -1, -1, -1, // *_fd
                                     &error) == NULL
        && errno == EINVAL,
        "sdexec_start_transient_unit with bad AllowedMemoryNodes fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    ok (sdexec_start_transient_unit_get (NULL, NULL) < 0 && errno == EINVAL,
        "sdexec_start_transient_unit_get f=NULL fails with EINVAL");

    json_decref (cmd_o_badnodes);
    json_decref (cmd_o_baddevice);
    json_decref (cmd_o_badprop);
    json_decref (cmd_o_noname);
    json_decref (cmd_o);
    flux_cmd_destroy (cmd);
    flux_future_destroy (f);
    flux_close (h);
}

void test_deviceallow (void)
{
    char *av[] = { "/bin/ls", NULL };
    int ac = 1;
    flux_t *h;
    flux_future_t *f;
    flux_cmd_t *cmd;
    json_t *cmd_o = NULL;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(cmd = flux_cmd_create (ac, av, environ))
        || flux_cmd_setopt (cmd, "SDEXEC_NAME", "foo.service") < 0
        || !(cmd_o = cmd_tojson (cmd)))
        BAIL_OUT ("could not create command object for testing");

    /* single entry */
    json_t *cmd_o_single = json_deep_copy (cmd_o);
    if (!cmd_o_single
        || jpath_set_new (cmd_o_single,
                          "opts.SDEXEC_PROP_DeviceAllow",
                          json_string ("/dev/null rw")) < 0)
        BAIL_OUT ("error preparing single DeviceAllow command");
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_single,
                                     -1, -1, -1, &error);
    ok (f != NULL, "sdexec_start_transient_unit with single DeviceAllow works");
    flux_future_destroy (f);
    json_decref (cmd_o_single);

    /* multiple entries */
    json_t *cmd_o_multi = json_deep_copy (cmd_o);
    if (!cmd_o_multi
        || jpath_set_new (cmd_o_multi,
                          "opts.SDEXEC_PROP_DeviceAllow",
                          json_string ("/dev/null rw,/dev/zero r")) < 0)
        BAIL_OUT ("error preparing multi DeviceAllow command");
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_multi,
                                     -1, -1, -1, &error);
    ok (f != NULL, "sdexec_start_transient_unit with multiple DeviceAllow works");
    flux_future_destroy (f);
    json_decref (cmd_o_multi);

    /* whitespace padding around entries and perms */
    json_t *cmd_o_ws = json_deep_copy (cmd_o);
    if (!cmd_o_ws
        || jpath_set_new (cmd_o_ws,
                          "opts.SDEXEC_PROP_DeviceAllow",
                          json_string (" /dev/null  rw , /dev/zero  r ")) < 0)
        BAIL_OUT ("error preparing whitespace DeviceAllow command");
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_ws,
                                     -1, -1, -1, &error);
    ok (f != NULL,
        "sdexec_start_transient_unit with whitespace-padded DeviceAllow works");
    flux_future_destroy (f);
    json_decref (cmd_o_ws);

    /* AllowedMemoryNodes uses the same bitmap encoding as AllowedCPUs */
    json_t *cmd_o_nodes = json_deep_copy (cmd_o);
    if (!cmd_o_nodes
        || jpath_set_new (cmd_o_nodes,
                          "opts.SDEXEC_PROP_AllowedMemoryNodes",
                          json_string ("0-1")) < 0)
        BAIL_OUT ("error preparing AllowedMemoryNodes command");
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_nodes,
                                     -1, -1, -1, &error);
    ok (f != NULL,
        "sdexec_start_transient_unit with AllowedMemoryNodes works");
    flux_future_destroy (f);
    json_decref (cmd_o_nodes);

    json_decref (cmd_o);
    flux_cmd_destroy (cmd);
    flux_close (h);
}

/* ExecStopPost is set from a JSON-encoded argv string and lands in the
 * StartTransientUnit request as an a(sasb) property alongside ExecStart.
 */
void test_execstoppost (void)
{
    char *av[] = { "/bin/ls", NULL };
    int ac = 1;
    flux_t *h;
    flux_future_t *f;
    flux_cmd_t *cmd;
    json_t *cmd_o = NULL;
    json_t *cmd_o_bad;
    flux_error_t error;
    const flux_msg_t *msg;
    json_t *props;
    size_t index;
    json_t *prop;
    const char *path = NULL;
    json_t *argv = NULL;
    const char *name;
    const char *mode;
    const char *type;
    json_t *aux;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(cmd = flux_cmd_create (ac, av, environ))
        || flux_cmd_setopt (cmd, "SDEXEC_NAME", "foo.service") < 0
        || flux_cmd_setopt (cmd,
                            "SDEXEC_PROP_ExecStopPost",
                            "[\"/bin/sh\", \"-c\", \"echo $1\", \"sh\", \"x\"]") < 0
        || !(cmd_o = cmd_tojson (cmd)))
        BAIL_OUT ("could not create command object for testing");

    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o, -1, -1, -1, &error);
    ok (f != NULL, "sdexec_start_transient_unit with ExecStopPost works");
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_unpack (msg,
                                NULL,
                                "{s:[ssoo]}",
                                "params", &name, &mode, &props, &aux) == 0,
        "request has a property array");
    /* property entry: [name, [type, value]]; a(sasb) value: [[path,argv,b]] */
    json_array_foreach (props, index, prop) {
        const char *pname;
        int ignore;
        if (json_unpack (prop,
                         "[s[s[[sob]]]]",
                         &pname, &type, &path, &argv, &ignore) == 0
            && streq (pname, "ExecStopPost")
            && streq (type, "a(sasb)"))
            break;
        path = NULL;
        argv = NULL;
    }
    ok (path != NULL
        && streq (path, "/bin/sh")
        && json_array_size (argv) == 5
        && streq (json_string_value (json_array_get (argv, 4)), "x"),
        "ExecStopPost property has the expected path and argv");
    flux_msg_decref (msg);
    flux_future_destroy (f);

    if (!(cmd_o_bad = json_deep_copy (cmd_o))
        || jpath_set_new (cmd_o_bad,
                          "opts.SDEXEC_PROP_ExecStopPost",
                          json_string ("not json")) < 0)
        BAIL_OUT ("could not create bad command object for testing");
    errno = 0;
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_bad, -1, -1, -1, &error);
    ok (f == NULL && errno == EINVAL,
        "sdexec_start_transient_unit with bad ExecStopPost fails with EINVAL");
    diag ("%s", error.text);
    json_decref (cmd_o_bad);

    if (!(cmd_o_bad = json_deep_copy (cmd_o))
        || jpath_set_new (cmd_o_bad,
                          "opts.SDEXEC_PROP_ExecStopPost",
                          json_string ("[1, 2]")) < 0)
        BAIL_OUT ("could not create bad command object for testing");
    errno = 0;
    f = sdexec_start_transient_unit (h, 0, "fail", cmd_o_bad, -1, -1, -1, &error);
    ok (f == NULL && errno == EINVAL,
        "sdexec_start_transient_unit with non-string ExecStopPost fails"
        " with EINVAL");
    json_decref (cmd_o_bad);

    json_decref (cmd_o);
    flux_cmd_destroy (cmd);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_inval ();
    test_deviceallow ();
    test_execstoppost ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
