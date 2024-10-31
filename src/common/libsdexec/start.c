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
#include <jansson.h>
#include <flux/core.h>
#include <float.h> // for DBL_MAX

#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/parse_size.h"

#include "parse.h"
#include "start.h"

/* This dense JSON format string deserves some explanation!
 * [s[sO]] is [key,[type,val]] std. form for a StartTransientUnit property.
 * - key is "ExecStart", the property name
 * - type is "a(sasb)", the D-Bus signature for value
 * - val is [[sOb]], an array of command lines
 * The command line [sOb] consists of
 * - command name (argv[0])
 * - argv array (of strings)
 * - boolean ignore-failure flag (e.g. an ExecStart prefix of "-")
 * This function assumes one command line, and ignore-failure=false.
 */
static int prop_add_execstart (json_t *prop, const char *name, json_t *cmdline)
{
    json_t *o = NULL;
    const char *arg0;

    if (json_unpack (cmdline, "[s]", &arg0) < 0
        || !(o = json_pack ("[s[s[[sOb]]]]", name, "a(sasb)", arg0, cmdline, 0))
        || json_array_append_new (prop, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
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
static int prop_add_env (json_t *prop, const char *name, json_t *dict)
{
    json_t *a;
    const char *k;
    json_t *vo;
    json_t *env;
    int rc = -1;

    if (!(a = json_array ()))
        return -1;
    json_object_foreach (dict, k, vo) {
        const char *v = json_string_value (vo);

        if (environment_name_ok (k)) {
            char *kv = NULL;
            json_t *kvo = NULL;
            if (asprintf (&kv, "%s=%s", k, v) < 0
                || !(kvo = json_string (kv))
                || json_array_append_new (a, kvo) < 0) {
                json_decref (kvo);
                free (kv);
                goto out;
            }
            free (kv);
        }
    }
    if (!(env = json_pack ("[s[sO]]", name, "as", a))
        || json_array_append_new (prop, env) < 0) {
        json_decref (env);
        goto out;
    }
    rc = 0;
out:
    json_decref (a);
    return rc;
}

static int prop_add_string (json_t *prop, const char *name, const char *val)
{
    json_t *o;

    if (val) {
        if (!(o = json_pack ("[s[ss]]", name, "s", val))
            || json_array_append_new (prop, o) < 0) {
            json_decref (o);
            return -1;
        }
    }
    return 0;
}

/* This assumes message source and destination are in the same process,
 * as is the case with sdexec => sdbus broker modules.
 */
static int prop_add_fd (json_t *prop, const char *name, int val)
{
    json_t *o;

    if (val >= 0) {
        if (!(o = json_pack ("[s[si]]", name, "h", val))
            || json_array_append_new (prop, o) < 0) {
            json_decref (o);
            return -1;
        }
    }
    return 0;
}

static int prop_add_bool (json_t *prop, const char *name, int val)
{
    json_t *o;

    if (!(o = json_pack ("[s[sb]]", name, "b", val))
        || json_array_append_new (prop, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
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

static int prop_add_u32 (json_t *prop, const char *name, uint32_t val)
{
    json_int_t i = val;
    json_t *o;

    if (!(o = json_pack ("[s[sI]]", name, "u", i))
        || json_array_append_new (prop, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

static int prop_add_u64 (json_t *prop, const char *name, uint64_t val)
{
    json_int_t i = (json_int_t)val;
    json_t *o;

    if (!(o = json_pack ("[s[sI]]", name, "t", i))
        || json_array_append_new (prop, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

static int prop_add_bytearray (json_t *prop,
                               const char *name,
                               uint8_t *bytearray,
                               size_t size)
{
    json_t *o;
    json_t *a;

    if (!(a = json_array ()))
        return -1;
    for (int i = 0; i < size; i++) {
        json_t *vo;
        if (!(vo = json_integer (bytearray[i]))
            || json_array_append_new (a, vo) < 0) {
            json_decref (vo);
            json_decref (a);
            return -1;
        }
    }
    if (!(o = json_pack ("[s[sO]]", name, "ay", a))
        || json_array_append_new (prop, o) < 0) {
        json_decref (o);
        json_decref (a);
        return -1;
    }
    json_decref (a);
    return 0;
}

/* Set a property by name. By default, values are strings  Those that are
 * not require explicit conversion from string.
 */
static int prop_add (json_t *prop, const char *name, const char *val)
{
    if (strlen (name) == 0 || !val)
        return 0;

    if (streq (name, "MemoryHigh")
        || streq (name, "MemoryMax")
        || streq (name, "MemoryMin")
        || streq (name, "MemoryLow")) {
        double d;
        uint64_t u;

        if (sdexec_parse_percent (val, &d) == 0) {
            char newname[64];
            snprintf (newname, sizeof (newname), "%sScale", name);
            if (prop_add_u32 (prop, newname, (uint32_t)(d * UINT32_MAX)) < 0)
                return -1;
        }
        else if (parse_size (val, &u) == 0) {
            if (prop_add_u64 (prop, name, u) < 0)
                return -1;
        }
        else if (streq (val, "infinity")) {
            if (prop_add_u64 (prop, name, UINT64_MAX) < 0)
                return -1;
        }
        else
            return -1;
    }
    else if (streq (name, "AllowedCPUs")) {
        uint8_t *bitmap;
        size_t size;

        if (sdexec_parse_bitmap (val, &bitmap, &size) < 0)
            return -1;
        if (prop_add_bytearray (prop, name, bitmap, size) < 0) {
            free (bitmap);
            return -1;
        }
        free (bitmap);
    }
    else if (streq (name, "SendSIGKILL")) {
        bool value;
        if (is_false (val))
            value = false;
        else if (is_true (val))
            value = true;
        else
            return -1;
        if (prop_add_bool (prop, name, value) < 0)
            return -1;
    }
    else {
        if (prop_add_string (prop, name, val) < 0)
            return -1;
    }
    return 0;
}

static json_t *prop_create (json_t *cmd,
                            const char *type,
                            int stdin_fd,
                            int stdout_fd,
                            int stderr_fd,
                            flux_error_t *error)
{
    json_t *prop;
    json_error_t jerror;
    const char *cwd = NULL;
    json_t *cmdline;
    json_t *env;
    json_t *opts;
    const char *key;
    json_t *val;

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
    if (!(prop = json_array ())) {
        errprintf (error, "out of memory");
        errno = ENOMEM;
        return NULL;
    }
    if (prop_add_execstart (prop, "ExecStart", cmdline) < 0
        || prop_add_string (prop, "Type", type) < 0
        || prop_add_string (prop, "WorkingDirectory", cwd) < 0
        || prop_add_bool (prop, "RemainAfterExit", true) < 0
        || prop_add_env (prop, "Environment", env) < 0
        || prop_add_fd (prop, "StandardInputFileDescriptor", stdin_fd) < 0
        || prop_add_fd (prop, "StandardOutputFileDescriptor", stdout_fd) < 0
        || prop_add_fd (prop, "StandardErrorFileDescriptor", stderr_fd) < 0) {
        errprintf (error, "error packing StartTransientUnit properties");
        goto error;
    }
    // any subprocess opt prefixed with SDEXEC_PROP_ is taken for a property
    json_object_foreach (opts, key, val) {
        if (strstarts (key, "SDEXEC_PROP_")) {
            if (prop_add (prop, key + 12, json_string_value (val)) < 0) {
                errprintf (error, "%s: error setting property", key);
                goto error;
            }
        }
    }
    return prop;
error:
    json_decref (prop);
    errno = EINVAL;
    return NULL;
}

flux_future_t *sdexec_start_transient_unit (flux_t *h,
                                            uint32_t rank,
                                            const char *mode,
                                            const char *type,
                                            json_t *cmd,
                                            int stdin_fd,
                                            int stdout_fd,
                                            int stderr_fd,
                                            flux_error_t *error)
{
    flux_future_t *f;
    json_t *prop = NULL;
    const char *name;

    if (!h || !mode || !type || !cmd) {
        errprintf (error, "invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(prop = prop_create (cmd,
                              type,
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
                             "{s:s s:[ssO[]]}",
                             "member", "StartTransientUnit",
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
