/************************************************************\

 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* start.c - start transient service unit from json-encoded flux_cmd_t
 *
 * Ref: https://www.freedesktop.org/wiki/Software/systemd/dbus/
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <stdio.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>
#include <float.h> // for DBL_MAX

#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/parse_size.h"
#include "src/common/libutil/strstrip.h"

#include "bus.h"
#include "parse.h"
#include "proplist.h"
#include "start.h"

/* ExecStart has D-Bus type a(sasb), an array of command lines.  Each is
 * [path, argv, ignore-failure], where ignore-failure corresponds to an
 * ExecStart prefix of "-".  This adds one command line with
 * ignore-failure=false.
 */
static void prop_add_execstart (struct sdexec_proplist *pl, json_t *cmdline)
{
    const char *arg0;
    json_t *val = NULL;

    if (json_unpack (cmdline, "[s]", &arg0) == 0)
        val = json_pack ("[[sOb]]", arg0, cmdline, 0);
    sdexec_proplist_add_json (pl, "ExecStart", "a(sasb)", val);
    json_decref (val);
}

/* systemd fails a StartTransientUnit request if environment variable
 * names start with a digit, or contain characters other than digits,
 * letters, or '_'.
 * https://github.com/systemd/systemd/blob/main/src/basic/env-util.c#L28
 */
static bool environment_name_ok (const char *name)
{
    if (strlen (name) == 0 || isdigit (name[0]))
        return false;
    for (int i = 0; i < strlen (name); i++) {
        if (!isalnum (name[i]) && name[i] != '_')
            return false;
    }
    return true;
}

/* The Environment property is an array of "key=value" strings, which
 * is built up from the env dict received as part of the command.
 */
static void prop_add_env (struct sdexec_proplist *pl, json_t *dict)
{
    json_t *a;
    const char *k;
    json_t *vo;

    if ((a = json_array ())) {
        json_object_foreach (dict, k, vo) {
            char *kv = NULL;
            if (!environment_name_ok (k))
                continue;
            if (asprintf (&kv, "%s=%s", k, json_string_value (vo)) < 0
                || json_array_append_new (a, json_string (kv)) < 0) {
                free (kv);
                json_decref (a);
                a = NULL; // proplist reports the error
                break;
            }
            free (kv);
        }
    }
    sdexec_proplist_add_json (pl, "Environment", "as", a);
    json_decref (a);
}

// per systemd.syntax(7), boolean values are: 1|yes|true|on, 0|no|false|off
static bool is_true (const char *s)
{
    if (streq (s, "1")
        || !strcasecmp (s, "yes")
        || !strcasecmp (s, "true")
        || !strcasecmp (s, "on"))
        return true;
    return false;
}
static bool is_false (const char *s)
{
    if (streq (s, "0")
        || !strcasecmp (s, "no")
        || !strcasecmp (s, "false")
        || !strcasecmp (s, "off"))
        return true;
    return false;
}

/* Parse comma-separated "specifier perms" pairs such as "/dev/nvidiactl rw"
 * into a JSON array for D-Bus type a(ss), as used by DeviceAllow.
 * See systemd.resource-control(5) for specifier and perms syntax.
 */
static json_t *parse_str_pair_array (const char *val)
{
    json_t *pairs;
    char *buf = NULL;
    char *saveptr;
    char *entry;

    if (!(pairs = json_array ()) || !(buf = strdup (val)))
        goto error;
    entry = strtok_r (buf, ",", &saveptr);
    while (entry) {
        char *sp;
        entry = strstrip (entry);
        if (!(sp = strchr (entry, ' ')))
            goto error;
        *sp++ = '\0';
        sp = strstrip (sp);
        if (strlen (entry) == 0
            || strlen (sp) == 0
            || json_array_append_new (pairs, json_pack ("[ss]", entry, sp)) < 0)
            goto error;
        entry = strtok_r (NULL, ",", &saveptr);
    }
    if (json_array_size (pairs) == 0)
        goto error;
    free (buf);
    return pairs;
error:
    free (buf);
    json_decref (pairs);
    return NULL;
}

/* Parse an unsigned integer that may also be "infinity" (UINT64_MAX).
 */
static int parse_u64_infinity (const char *val, uint64_t *up)
{
    char *endptr;

    if (streq (val, "infinity")) {
        *up = UINT64_MAX;
        return 0;
    }
    errno = 0;
    *up = strtoull (val, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return -1;
    return 0;
}

/* Set a property by name from its string value.  By default, values are
 * strings.  Those that are not require explicit conversion from string.
 * Return -1 if 'val' cannot be parsed for property 'name'.
 */
static int prop_add (struct sdexec_proplist *pl,
                     const char *name,
                     const char *val)
{
    if (strlen (name) == 0 || !val)
        return 0;

    if (streq (name, "MemoryHigh")
        || streq (name, "MemoryMax")
        || streq (name, "MemoryMin")
        || streq (name, "MemoryLow")
        || streq (name, "MemorySwapMax")) {
        double d;
        uint64_t u;

        if (sdexec_parse_percent (val, &d) == 0) {
            char newname[64];
            snprintf (newname, sizeof (newname), "%sScale", name);
            sdexec_proplist_add (pl, newname, "u", (uint32_t)(d * UINT32_MAX));
        }
        else if (parse_size (val, &u) == 0)
            sdexec_proplist_add (pl, name, "t", u);
        else if (streq (val, "infinity"))
            sdexec_proplist_add (pl, name, "t", UINT64_MAX);
        else
            return -1;
    }
    else if (streq (name, "AllowedCPUs")
        || streq (name, "AllowedMemoryNodes")) {
        uint8_t *bitmap;
        size_t size;

        if (sdexec_parse_bitmap (val, &bitmap, &size) < 0)
            return -1;
        sdexec_proplist_add_array (pl, name, "y", bitmap, size);
        free (bitmap);
    }
    else if (streq (name, "DeviceAllow")) {
        json_t *pairs;

        if (!(pairs = parse_str_pair_array (val)))
            return -1;
        sdexec_proplist_add_json (pl, name, "a(ss)", pairs);
        json_decref (pairs);
    }
    else if (streq (name, "SendSIGKILL")) {
        if (is_false (val))
            sdexec_proplist_add (pl, name, "b", false);
        else if (is_true (val))
            sdexec_proplist_add (pl, name, "b", true);
        else
            return -1;
    }
    else if (streq (name, "TimeoutStopUSec")) {
        uint64_t u;

        if (parse_u64_infinity (val, &u) < 0)
            return -1;
        sdexec_proplist_add (pl, name, "t", u);
    }
    else if (streq (name, "OOMScoreAdjust")) {
        char *endptr;
        long l;

        errno = 0;
        l = strtol (val, &endptr, 10);
        if (errno != 0 || *endptr != '\0')
            return -1;
        sdexec_proplist_add (pl, name, "i", (int)l);
    }
    else
        sdexec_proplist_add (pl, name, "s", val);
    return 0;
}

static json_t *prop_create (json_t *cmd,
                            int stdin_fd,
                            int stdout_fd,
                            int stderr_fd,
                            flux_error_t *error)
{
    struct sdexec_proplist *pl;
    json_t *prop = NULL;
    json_error_t jerror;
    const char *cwd = NULL;
    json_t *cmdline;
    json_t *env;
    json_t *opts;
    const char *key;
    json_t *val;
    bool type_is_set = false;

    // Not unpacked: channels
    if (json_unpack_ex (cmd,
                        &jerror,
                        0,
                        "{s?s s:o s:o s:o}",
                        "cwd", &cwd,
                        "cmdline", &cmdline,
                        "env", &env,
                        "opts", &opts) < 0
        || json_array_size (cmdline) == 0) {
        errprintf (error, "error parsing command object: %s", jerror.text);
        errno = EPROTO;
        return NULL;
    }
    if (!(pl = sdexec_proplist_create ())) {
        errprintf (error, "out of memory");
        errno = ENOMEM;
        return NULL;
    }
    prop_add_execstart (pl, cmdline);
    if (cwd)
        sdexec_proplist_add (pl, "WorkingDirectory", "s", cwd);
    sdexec_proplist_add (pl, "RemainAfterExit", "b", true);
    prop_add_env (pl, env);
    // N.B. this assumes sdexec and sdbus are in the same process (RFC 52)
    if (stdin_fd >= 0)
        sdexec_proplist_add (pl,
                             "StandardInputFileDescriptor",
                             "h",
                             stdin_fd);
    if (stdout_fd >= 0)
        sdexec_proplist_add (pl,
                             "StandardOutputFileDescriptor",
                             "h",
                             stdout_fd);
    if (stderr_fd >= 0)
        sdexec_proplist_add (pl,
                             "StandardErrorFileDescriptor",
                             "h",
                             stderr_fd);

    // any subprocess opt prefixed with SDEXEC_PROP_ is taken for a property
    json_object_foreach (opts, key, val) {
        if (strstarts (key, "SDEXEC_PROP_")) {
            if (prop_add (pl, key + 12, json_string_value (val)) < 0) {
                errprintf (error, "%s: error setting property", key);
                errno = EINVAL;
                goto done;
            }
            if (streq (key + 12, "Type"))
                type_is_set = true;
        }
    }
    if (!type_is_set)
        sdexec_proplist_add (pl, "Type", "s", "exec");
    prop = sdexec_proplist_finish (pl, error);
done:
    sdexec_proplist_destroy (pl);
    return prop;
}

flux_future_t *sdexec_start_transient_unit (flux_t *h,
                                            uint32_t rank,
                                            const char *mode,
                                            json_t *cmd,
                                            int stdin_fd,
                                            int stdout_fd,
                                            int stderr_fd,
                                            flux_error_t *error)
{
    flux_future_t *f;
    json_t *prop = NULL;
    const char *name;

    if (!h || !mode || !cmd) {
        errprintf (error, "invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(prop = prop_create (cmd,
                              stdin_fd,
                              stdout_fd,
                              stderr_fd,
                              error)))
        return NULL;
    if (json_unpack (cmd, "{s:{s:s}}", "opts", "SDEXEC_NAME", &name) < 0) {
        errprintf (error, "SDEXEC_NAME subprocess command option is not set");
        errno = EINVAL;
        goto error;
    }
    /* N.B. the empty array tacked onto the end of the 'params' array below
     * is the placeholder for aux unit info, unused here.
     */
    if (!(f = flux_rpc_pack (h,
                             "sdbus.call",
                             rank,
                             0,
                             "{s:s s:s s:s s:s s:s s:[ssO[]]}",
                             "destination", SDEXEC_DESTINATION,
                             "path", SDEXEC_MANAGER_PATH,
                             "interface", SDEXEC_MANAGER_IFACE,
                             "member", "StartTransientUnit",
                             "signature", "ssa(sv)a(sa(sv))",
                             "params", name, mode, prop))) {
        errprintf (error, "error sending StartTransientUnit RPC");
        goto error;
    }
    json_decref (prop);
    return f;
error:
    ERRNO_SAFE_WRAP (json_decref, prop);
    return NULL;
}

int sdexec_start_transient_unit_get (flux_future_t *f, const char **jobp)
{
    const char *job;

    if (flux_rpc_get_unpack (f, "{s:[s]}", "params", &job) < 0)
        return -1;
    if (jobp)
        *jobp = job;
    return 0;
}

// vi:ts=4 sw=4 expandtab
