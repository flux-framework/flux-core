/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* imp_exec_helper.c - produce IMP exec helper JSON on stdout
 *
 * Usage: flux imp_exec_helper JOBID
 *
 * Fetch the signed jobspec (J) for JOBID and emit it as JSON to stdout
 * for consumption by the IMP exec helper protocol (RFC 15).
 *
 * If INVOCATION_ID is set (i.e. we are running inside a systemd unit),
 * also query the unit's DevicePolicy and DeviceAllow properties via
 * sdbus and include an "options" object so the IMP can configure device
 * containment.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>

#include "ccan/str/str.h"
#include "ccan/str/hex/hex.h"
#include "builtin.h"

static const char *unit_interface = "org.freedesktop.systemd1.Service";
static const char *prop_interface = "org.freedesktop.DBus.Properties";

static struct optparse_option helper_opts[] = {
    { .name = "test-nojob", .key = 0, .has_arg = 0,
      .usage = "Skip J lookup, emit options only (for testing)" },
    OPTPARSE_TABLE_END,
};

/* Decode a 32-char hex invocation ID string to a JSON array of 16 bytes.
 * The caller must json_decref() the result.
 */
static json_t *invocation_id_decode (const char *hex)
{
    unsigned char data[16];
    json_t *a;

    if (!hex_decode (hex, strlen (hex), data, sizeof (data)))
        log_msg_exit ("invalid INVOCATION_ID: %s", hex);
    if (!(a = json_array ()))
        log_err_exit ("error creating INVOCATION_ID byte array");
    for (int i = 0; i < sizeof (data); i++) {
        if (json_array_append_new (a, json_integer (data[i])) < 0)
            log_err_exit ("error building INVOCATION_ID byte array");
    }
    return a;
}

/* Call GetUnitByInvocationID and return the decoded unit object path.
 * The caller must free() the result.
 */
static char *get_unit_path (flux_t *h, const char *invocation_id)
{
    json_t *bytes;
    flux_future_t *f;
    const char *path;
    char *result;

    bytes = invocation_id_decode (invocation_id);
    if (!(f = flux_rpc_pack (h,
                             "sdbus.call",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:[o]}",
                             "member", "GetUnitByInvocationID",
                             "params", bytes))
        || flux_rpc_get_unpack (f, "{s:[s]}", "params", &path) < 0)
        log_msg_exit ("GetUnitByInvocationID: %s", future_strerror (f, errno));
    if (!(result = strdup (path)))
        log_err_exit ("error duplicating unit path");
    json_decref (bytes);
    flux_future_destroy (f);
    return result;
}

/* Build an options object from the systemd unit's device containment
 * properties via sdbus.  A NULL return indicates "no containment".
 * Errors communicating with systemd or parsing results are fatal.
 * The caller must json_decref() the returned object.
 */
static json_t *options_from_unit (flux_t *h)
{
    const char *invocation_id;
    char *unit_path;
    flux_future_t *f;
    flux_future_t *f2;
    const char *vtype;
    const char *policy;
    json_t *allow;
    json_t *options = NULL;

    if (!(invocation_id = getenv ("INVOCATION_ID")))
        return NULL; // not running in a systemd unit
    unit_path = get_unit_path (h, invocation_id);

    /* Fetch DevicePolicy (strict, closed, or auto) and DeviceAllow array.
     * Pass them through to the IMP options unchanged; the IMP normalizes.
     */
    if (!(f = flux_rpc_pack (h,
                             "sdbus.call",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:s s:s s:[ss]}",
                             "path", unit_path,
                             "interface", prop_interface,
                             "member", "Get",
                             "params", unit_interface, "DevicePolicy"))
        || flux_rpc_get_unpack (f,
                                "{s:[[ss]]}",
                                "params", &vtype, &policy) < 0)
        log_msg_exit ("Get DevicePolicy: %s", future_strerror (f, errno));

    if (!(f2 = flux_rpc_pack (h,
                              "sdbus.call",
                              FLUX_NODEID_ANY,
                              0,
                              "{s:s s:s s:s s:[ss]}",
                              "path", unit_path,
                              "interface", prop_interface,
                              "member", "Get",
                              "params", unit_interface, "DeviceAllow"))
        || flux_rpc_get_unpack (f2,
                                "{s:[[so]]}",
                                "params", &vtype, &allow) < 0)
        log_msg_exit ("Get DeviceAllow: %s", future_strerror (f2, errno));

    if (!(options = json_pack ("{s:s s:O}",
                               "DevicePolicy", policy,
                               "DeviceAllow", allow)))
        log_msg_exit ("error preparing device options for IMP");
    flux_future_destroy (f2);
    flux_future_destroy (f);
    free (unit_path);
    return options;
}

/* Fetch the signed jobspec J for jobid.
 * Caller must json_decref() the result.
 */
static json_t *J_from_jobid (flux_t *h, const char *jobid)
{
    flux_jobid_t id;
    flux_future_t *f;
    const char *J;
    json_t *result;

    if (!jobid)
        log_msg_exit ("Unable to determine jobid");
    if (flux_job_id_parse (jobid, &id) < 0)
        log_err_exit ("invalid jobid: %s", jobid);

    if (!(f = flux_rpc_pack (h,
                             "job-info.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:[s] s:i}",
                             "id", id,
                             "keys", "J",
                             "flags", 0))
        || flux_rpc_get_unpack (f, "{s:s}", "J", &J) < 0)
        log_err_exit ("error looking up J: %s", future_strerror (f, errno));
    if (!(result = json_string (J)))
        log_msg_exit ("error creating JSON string for J");
    flux_future_destroy (f);
    return result;
}

static int helper (optparse_t *p, int ac, char *av[])
{
    int optindex;
    const char *jobid;
    flux_t *h;
    json_t *imp_input;
    json_t *val;

    log_init ("imp_exec_helper");

    optindex = optparse_option_index (p);

    if (optparse_hasopt (p, "test-nojob") && optindex != ac)
        log_msg_exit ("--test-nojob cannot be used with JOBID argument");
    if (ac - optindex > 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < ac)
        jobid = av[optindex];
    else
        jobid = getenv ("FLUX_JOB_ID");

    h = builtin_get_flux_handle (p);

    if (!(imp_input = json_object ()))
        log_msg_exit ("error creating imp input object");

    if (!optparse_hasopt (p, "test-nojob")) {
        if (json_object_set_new (imp_input, "J", J_from_jobid (h, jobid)) < 0)
            log_msg_exit ("error adding J to imp input");
    }
    if ((val = options_from_unit (h))) {
        if (json_object_set_new (imp_input, "options", val) < 0)
            log_msg_exit ("error adding options to imp input");
    }

    if (json_dumpf (imp_input, stdout, JSON_COMPACT) < 0)
        log_msg_exit ("error writing imp input object to stdout");

    json_decref (imp_input);
    return 0;
}

int subcommand_imp_exec_helper_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "imp_exec_helper",
        helper,
        "[JOBID]",
        "Emit IMP exec helper JSON for JOBID",
        0,
        helper_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
