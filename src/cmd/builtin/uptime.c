/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <time.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/fsd.h"

#include "builtin.h"

/* Fetch the named broker group idset membership, and return the
 * number of members.
 */
static int groups_get_count (flux_t *h, const char *name)
{
    flux_future_t *f;
    const char *members;
    struct idset *ids;
    int count;

    if (!(f = flux_rpc_pack (h, "groups.get", 0, 0, "{s:s}", "name", name))
        || flux_rpc_get_unpack (f, "{s:s}", "members", &members) < 0
        || !(ids = idset_decode (members)))
        log_err_exit ("Error fetching %s group", name);
    count = idset_count (ids);
    idset_destroy (ids);
    flux_future_destroy (f);
    return count;
}

/* Return true if scheduling is disabled.
 */
static bool sched_disabled (flux_t *h)
{
    flux_future_t *f;
    int enable;

    if (!(f = flux_rpc_pack (h,
                             "job-manager.alloc-admin",
                             0,
                             0,
                             "{s:b s:b s:s}",
                             "query_only", 1,
                             "enable", 0,
                             "reason", ""))
        || flux_rpc_get_unpack (f, "{s:b}", "enable", &enable) < 0)
        log_err_exit ("Error fetching alloc status");
    return enable ? false : true;
}

/* Return true if job submission is disabled.
 */
static bool submit_disabled (flux_t *h)
{
    flux_future_t *f;
    int enable;

    if (!(f = flux_rpc_pack (h,
                             "job-manager.submit-admin",
                             0,
                             0,
                             "{s:b s:b s:s}",
                             "query_only", 1,
                             "enable", 0,
                             "reason", ""))
        || flux_rpc_get_unpack (f, "{s:b}", "enable", &enable) < 0)
        log_err_exit ("Error fetching submit status");
    return enable ? false : true;
}

/* Each key in the drain object is an idset representing a group
 * of drained nodes.  Sum the member count of all idsets and set 'countp'.
 * Return 0 on success, -1 on failure.
 */
static int parse_drain_object (json_t *drain, int *countp)
{
    const char *key;
    json_t *value;
    struct idset *ids;
    int count = 0;

    json_object_foreach (drain, key, value) {
        if (!(ids = idset_decode (key)))
            return -1;
        count += idset_count (ids);
        idset_destroy (ids);
    }
    *countp = count;
    return 0;
}

/* Get the number of drained nodes.
 */
static int resource_status_drained (flux_t *h)
{
    flux_future_t *f;
    int count;
    json_t *drain;

    if (!(f = flux_rpc (h, "resource.status", NULL, 0, 0))
        || flux_rpc_get_unpack (f, "{s:o}", "drain", &drain) < 0
        || parse_drain_object (drain, &count) < 0)
        log_err_exit ("Error fetching resource status");
    flux_future_destroy (f);
    return count;
}

/* Fetch an attribute and return its value as integer.
 */
static int attr_get_int (flux_t *h, const char *name)
{
    const char *s;
    char *endptr;
    int i;

    if (!(s = flux_attr_get (h, name)))
        log_err_exit ("Error fetching %s attribute", name);
    errno = 0;
    i = strtol (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_msg_exit ("Error parsing %s", name);
    return i;
}

/* Fetch broker.starttime from rank 0 and return its value as double.
 */
static double attr_get_starttime (flux_t *h)
{
    const char *name = "broker.starttime";
    flux_future_t *f;
    const char *s;
    char *endptr;
    double d;

    if (!(f = flux_rpc_pack (h, "attr.get", 0, 0, "{s:s}", "name", name))
        || flux_rpc_get_unpack (f, "{s:s}", "value", &s) < 0)
        log_err_exit ("Error fetching %s attribute", name);
    errno = 0;
    d = strtod (s, &endptr);
    if (errno != 0 || *endptr != '\0')
        log_msg_exit ("Error parsing %s", name);
    return d;
}

/* Format seconds-since-epoch time in HH:MM:SS (24-hour) format.
 */
static int format_time (char *buf, size_t len, double t)
{
    struct tm tm;
    time_t tt = t;

    if (localtime_r (&tt, &tm) == NULL)
        return -1;
    if (strftime (buf, len, "%T", &tm) == 0)
        return -1;
    return 0;
}

/* Get the username for 'uid'.  If that fails, convert uid to string.
 */
static int format_user (char *buf, size_t len, int uid)
{
    char pwbuf[16384];
    struct passwd pwd;
    struct passwd *result;
    int n;

    if (getpwuid_r (uid, &pwd, pwbuf, sizeof (pwbuf), &result) < 0)
        n = snprintf (buf, len, "%d", uid);
    else
        n = snprintf (buf, len, "%s", result->pw_name);
    if (n >= len)
        return -1;
    return 0;
}

/* Append ",  name" to buf if condition is true.
 * Ignore truncation errors.
 */
static void append_if (char *buf,
                       size_t len,
                       const char *name,
                       bool condition)
{
    if (condition) {
        int n = strlen (buf);
        snprintf (buf + n,
                  len - n,
                  "%s%s",
                  n > 0 ? ",  " : "",
                  name);
    }
}

/* Append ",  count name" to buf if condition is true
 * Ignore truncation errors.
 */
static void append_count_if (char *buf,
                             size_t len,
                             const char *name,
                             int count,
                             bool condition)
{
    if (condition) {
        int n = strlen (buf);
        snprintf (buf + n,
                  len - n,
                  ",  %d %s",
                  count,
                  name);
    }
}

/* Mimic uptime(1), sort of.
 */
static void default_summary (flux_t *h)
{
    double t_now = flux_reactor_now (flux_get_reactor (h));
    int userid = attr_get_int (h, "security.owner");
    int size = attr_get_int (h, "size");
    int level = attr_get_int (h, "instance-level");
    int drained = 0;
    int offline = 0;
    bool submit_is_disabled = false;
    bool sched_is_disabled = false;
    flux_future_t *f;
    const char *broker_state;
    double duration;
    char fsd[32];
    char now[32];
    char owner[32];
    char extra[512] = "";

    /* Fetch the broker state.
     * If it is "run", proceed to fetch info from high level services and
     * the rank 0 broker and set duration to the instance runtime.
     * Otherwise, an abbreviated set of info will be displayed and let the
     * duration reflect the local broker's time in the current state.
     */
    if (!(f = flux_rpc (h, "state-machine.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f,
                                "{s:s s:f}",
                                "state", &broker_state,
                                "duration", &duration) < 0)
        log_err_exit ("Error fetching broker state");
    if (!strcmp (broker_state, "run")) {
        duration = t_now - attr_get_starttime (h);
        drained = resource_status_drained (h);
        offline = size - groups_get_count (h, "broker.online");
        submit_is_disabled = submit_disabled (h);
        sched_is_disabled = sched_disabled (h);
    }
    if (fsd_format_duration_ex (fsd, sizeof (fsd), duration, 2) < 0)
        log_err_exit ("Error formatting uptime duration");
    if (format_time (now, sizeof (now), t_now) < 0)
        log_msg_exit ("Error formatting current time");
    if (format_user (owner, sizeof (owner), userid) < 0)
        log_msg_exit ("Error formatting instance owner");
    append_count_if (extra, sizeof (extra), "drained", drained, drained > 0);
    append_count_if (extra, sizeof (extra), "offline", offline, offline > 0);
    printf (" %s %s %s,  owner %s,  depth %d,  size %d%s\n",
            now,
            broker_state,
            fsd,
            owner,
            level,
            size,
            extra);

    /* optional 2nd line for submit/sched disabled
     */
    extra[0] = '\0';
    append_if (extra, sizeof (extra), "submit disabled", submit_is_disabled);
    append_if (extra, sizeof (extra), "scheduler disabled", sched_is_disabled);
    if (strlen (extra) > 0)
        printf ("  %s\n", extra);

    flux_future_destroy (f);
}

static int cmd_uptime (optparse_t *p, int ac, char *av[])
{
    flux_t *h = builtin_get_flux_handle (p);

    default_summary (h);

    return (0);
}

int subcommand_uptime_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "uptime",
        cmd_uptime,
        NULL,
        "Show how long this Flux instance has been running",
        0,
        NULL);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
