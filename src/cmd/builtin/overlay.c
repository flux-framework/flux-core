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
# include <config.h>
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libccan/ccan/str/str.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libhostlist/hostlist.h"

#include "builtin.h"

static const char *ansi_default = "\033[39m";
static const char *ansi_red = "\033[31m";
static const char *ansi_yellow = "\033[33m";
//static const char *ansi_green = "\033[32m";
static const char *ansi_dark_gray = "\033[90m";

static const char *vt100_mode_line = "\033(0";
static const char *vt100_mode_normal = "\033(B";

static struct optparse_option status_opts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "NODEID",
      .usage = "Check health of subtree rooted at NODEID (default 0)",
    },
    { .name = "verbose", .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Increase reporting detail"
               " (1=lost/offline nodes, 2=degraded/partial trees, 3=full)",
    },
    { .name = "timeout", .key = 't', .has_arg = 1, .arginfo = "FSD",
      .usage = "Set RPC timeout (default none)",
    },
    { .name = "hostnames", .key = 'H', .has_arg = 0,
      .usage = "Display hostnames instead of ranks",
    },
    { .name = "times", .key = 'T', .has_arg = 0,
      .usage = "Show round trip RPC times",
    },
    { .name = "pretty", .key = 'p', .has_arg = 0,
      .usage = "Indent entries and use line drawing characters"
               " to show overlay tree structure",
    },
    { .name = "ghost", .key = 'g', .has_arg = 0,
      .usage = "Fill in presumed state of nodes that are"
               " inaccessible behind offline/lost overlay parents",
    },
    { .name = "color", .key = 'c', .has_arg = 0,
      .usage = "Use color to highlight offline/lost nodes",
    },
    { .name = "since", .key = 's', .has_arg = 0,
      .usage = "Show time since current state was entered",
    },
    { .name = "wait", .key = 'w', .has_arg = 1, .arginfo = "STATE",
      .usage = "Wait until subtree enters STATE before reporting"
               " (full, partial, offline, degraded, lost)",
    },
    OPTPARSE_TABLE_END
};

struct status {
    flux_t *h;
    int verbose;
    double timeout;
    struct hostlist *hl;
    optparse_t *opt;
    struct timespec start;
    const char *wait;
};

struct status_node {
    int rank;
    const char *status;
    double duration;
    bool ghost;
};

typedef bool (*map_f)(struct status *ctx,
                      struct status_node *node,
                      bool parent,
                      int level);

static const char *status_duration (struct status *ctx, double since)
{
    char dbuf[128];
    static char buf[256];

    if (!optparse_hasopt (ctx->opt, "since")
        || since <= 0.
        || fsd_format_duration (dbuf, sizeof (dbuf), since) < 0)
        return "";
    snprintf (buf, sizeof (buf), " for %s", dbuf);
    return buf;
}

static const char *status_colorize (struct status *ctx,
                                    const char *status,
                                    bool ghost)
{
    static char buf[128];

    if (optparse_hasopt (ctx->opt, "color")) {
        if (streq (status, "lost") && !ghost) {
            snprintf (buf, sizeof (buf), "%s%s%s",
                      ansi_red, status, ansi_default);
            status = buf;
        }
        else if (streq (status, "offline") && !ghost) {
            snprintf (buf, sizeof (buf), "%s%s%s",
                      ansi_yellow, status, ansi_default);
            status = buf;
        }
        else if (ghost) {
            snprintf (buf, sizeof (buf), "%s%s%s",
                      ansi_dark_gray, status, ansi_default);
            status = buf;
        }
    }
    return status;
}

static const char *status_indent (struct status *ctx, int n)
{
    static char buf[1024];
    if (!optparse_hasopt (ctx->opt, "pretty") || n == 0)
        return "";
    snprintf (buf, sizeof (buf), "%*s%s%s%s", n - 1, "",
              vt100_mode_line,
              "m", // '|_'
              vt100_mode_normal);
    return buf;
}

/* Return string containing the "best" name for node.
 * If --hostnames, look up the hostname.
 * Otherwise, just make a string out of the rank.
 */
static const char *status_getname (struct status *ctx, int rank)
{
    static char buf[128];
    const char *s;

    if (ctx->hl && (s = hostlist_nth (ctx->hl, rank)) != NULL)
        snprintf (buf, sizeof (buf), "%s", s);
    else
        snprintf (buf, sizeof (buf), "%d", rank);
    return buf;
}

/* If --times, return string containing parenthesised elapsed
 * time since last RPC was started, with leading space.
 * Otherwise, return the empty string
 */
static const char *status_rpctime (struct status *ctx)
{
    static char buf[64];
    if (!optparse_hasopt (ctx->opt, "times"))
        return "";
    snprintf (buf, sizeof (buf), " (%.3f ms)", monotime_since (ctx->start));
    return buf;
}

static void status_print (struct status *ctx,
                          struct status_node *node,
                          bool parent,
                          int level)
{
    printf ("%s%s: %s%s%s\n",
            status_indent (ctx, level),
            status_getname (ctx, node->rank),
            status_colorize (ctx, node->status, node->ghost),
            status_duration (ctx, node->duration),
            parent ? status_rpctime (ctx) : "");
}

static void status_print_noname (struct status *ctx,
                                 struct status_node *node,
                                 bool parent,
                                 int level)
{
    printf ("%s%s%s%s\n",
            status_indent (ctx, level),
            status_colorize (ctx, node->status, node->ghost),
            status_duration (ctx, node->duration),
            parent ? status_rpctime (ctx) : "");
}

/* Look up topology of 'child_rank' within the subtree topology rooted
 * at 'parent_rank'. Caller must json_decref() the result.
 * Returns NULL if --ghost option was not provided, or the lookup fails.
 */
static json_t *topo_lookup (struct status *ctx,
                            int parent_rank,
                            int child_rank)
{
    flux_future_t *f;
    json_t *topo;

    if (!optparse_hasopt (ctx->opt, "ghost"))
        return NULL;
    if (!(f = flux_rpc_pack (ctx->h,
                             "overlay.topology",
                             parent_rank,
                             0,
                             "{s:i}",
                             "rank", child_rank))
        || flux_future_wait_for (f, ctx->timeout) < 0
        || flux_rpc_get_unpack (f, "o", &topo) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    json_incref (topo);
    flux_future_destroy (f);
    return topo;
}

/* Walk a "ghost" subtree from the fixed topology.  Each node is assumed to
 * have the same 'status' as the offline/lost parent at the subtree root.
 * This augments healthwalk() to fill in nodes that would otherwise be missing
 * because they don't have a direct parent that is online for probing.
 * N.B. the starting point, the rank at the root of topo, is assumed to
 * have already been mapped/iterated over.
 */
static void status_ghostwalk (struct status *ctx,
                              json_t *topo,
                              int level,
                              const char *status,
                              map_f fun)
{
    json_t *children;
    size_t index;
    json_t *entry;
    struct status_node node = {
        .status = status,
        .duration = -1., // invalid - don't print
        .ghost = true,
    };

    if (json_unpack (topo, "{s:o}", "children", &children) < 0)
        return;
    json_array_foreach (children, index, entry) {
        if (json_unpack (entry, "{s:i}", "rank", &node.rank) < 0)
            return;
        if (fun (ctx, &node, false, level + 1))
            status_ghostwalk (ctx, entry, level + 1, status, fun);
    }
}

static flux_future_t *health_rpc (struct status *ctx, int rank)
{
    flux_future_t *f;

    if (ctx->wait) {
        f = flux_rpc_pack (ctx->h,
                           "overlay.health",
                           rank,
                           0,
                           "{s:s}",
                           "wait", ctx->wait);
        ctx->wait = NULL; // only wait on --rank, not subsequent probing
    }
    else {
        f = flux_rpc_pack (ctx->h,
                           "overlay.health",
                           rank,
                           0,
                           "{}");
    }
    return f;
}

/* Execute fun() for each online broker in subtree rooted at 'rank'.
 * If fun() returns true, follow tree to the broker's children.
 * If false, don't go down that path.
 */
static int status_healthwalk (struct status *ctx,
                              int rank,
                              int level,
                              map_f fun)
{
    struct status_node node = { .ghost = false };
    flux_future_t *f;
    json_t *children;
    int rc = 0;

    monotime (&ctx->start);

    if (!(f = health_rpc (ctx, rank))
        || flux_future_wait_for (f, ctx->timeout) < 0
        || flux_rpc_get_unpack (f,
                                "{s:i s:s s:f s:o}",
                                "rank", &node.rank,
                                "status", &node.status,
                                "duration", &node.duration,
                                "children", &children) < 0) {
        /* RPC failed.
         * An error at level 0 should be fatal, e.g. unknown wait argument,
         * bad rank, timeout.  An error at level > 0 should return -1 so
         * ghostwalk() can be tried (parent hasn't noticed child crash?)
         * and sibling subtrees can be probed.
         */
        if (level == 0)
            log_msg_exit ("%s", future_strerror (f, errno));
        printf ("%s%s: %s%s\n",
                status_indent (ctx, level),
                status_getname (ctx, rank),
                future_strerror (f, errno),
                status_rpctime (ctx));
        rc = -1;
        goto done;
    }
    if (fun (ctx, &node, true, level)) {
        if (children) {
            size_t index;
            json_t *entry;
            struct status_node child = { .ghost = false };
            json_t *topo;

            json_array_foreach (children, index, entry) {
                if (json_unpack (entry,
                                 "{s:i s:s s:f}",
                                 "rank", &child.rank,
                                 "status", &child.status,
                                 "duration", &child.duration) < 0)
                    log_msg_exit ("error parsing child array entry");
                if (fun (ctx, &child, false, level + 1)) {
                    if (streq (child.status, "offline")
                        || streq (child.status, "lost")
                        || status_healthwalk (ctx, child.rank,
                                              level + 1, fun) < 0) {
                        topo = topo_lookup (ctx, node.rank, child.rank);
                        status_ghostwalk (ctx, topo, level + 1, child.status, fun);
                    }
                }
            }
        }
    }
done:
    flux_future_destroy (f);
    return rc;
}

/* map fun - print the first entry without adornment and stop the walk.
 */
static bool show_top (struct status *ctx,
                      struct status_node *node,
                      bool parent,
                      int level)
{
    status_print_noname (ctx, node, parent, level);
    return false;
}

/* map fun - only follow degraded/partial, only print lost/offline (leaves).
 */
static bool show_badleaves (struct status *ctx,
                            struct status_node *node,
                            bool parent,
                            int level)
{
    if (level == 0 && streq (node->status, "full"))
        status_print_noname (ctx, node, parent, level);
    else if (streq (node->status, "lost") || streq (node->status, "offline"))
        status_print (ctx, node, parent, level);
    if (streq (node->status, "full"))
        return false;
    return true;
}

/* map fun - only follow degraded/partial, but print all non-full nodes.
 */
static bool show_badtrees (struct status *ctx,
                           struct status_node *node,
                           bool parent,
                           int level)
{
    if (parent
        || streq (node->status, "lost")
        || streq (node->status, "offline"))
        status_print (ctx, node, parent, level);
    if (streq (node->status, "full"))
        return false;
    return true;
}

/* map fun - follow all live brokers and print everything
 */
static bool show_all (struct status *ctx,
                      struct status_node *node,
                      bool parent,
                      int level)
{
    if (parent
        || streq (node->status, "lost")
        || streq (node->status, "offline"))
        status_print (ctx, node, parent, level);
    return true;
}

static int subcmd_status (optparse_t *p, int ac, char *av[])
{
    int rank = optparse_get_int (p, "rank", 0);
    struct status ctx;
    map_f fun;

    ctx.h = builtin_get_flux_handle (p);
    ctx.verbose = optparse_get_int (p, "verbose", 0);
    ctx.timeout = optparse_get_duration (p, "timeout", -1.0);
    ctx.hl = NULL;
    ctx.opt = p;
    ctx.wait = optparse_get_str (p, "wait", NULL);

    if (optparse_hasopt (p, "hostnames")) {
        const char *s = flux_attr_get (ctx.h, "config.hostlist");
        if (!s)
            log_err_exit ("config.hostlist attribute is not set");
        if (!(ctx.hl = hostlist_decode (s)))
            log_err_exit ("config.hostlist value could not be decoded");
    }

    if (ctx.verbose <= 0)
        fun = show_top;
    else if (ctx.verbose == 1)
        fun = show_badleaves;
    else if (ctx.verbose == 2)
        fun = show_badtrees;
    else
        fun = show_all;

    status_healthwalk (&ctx, rank, 0, fun);

    hostlist_destroy (ctx.hl);
    return 0;
}

int cmd_overlay (optparse_t *p, int argc, char *argv[])
{
    log_init ("flux-overlay");

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_subcommand overlay_subcmds[] = {
    { "status",
      "[OPTIONS]",
      "Display overlay subtree health status",
      subcmd_status,
      0,
      status_opts,
    },
    OPTPARSE_SUBCMD_END
};


int subcommand_overlay_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
        "overlay",
        cmd_overlay,
        NULL,
        "Manage overlay network",
        0,
        NULL);
    if (e != OPTPARSE_SUCCESS)
        return -1;

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "overlay"),
                                  overlay_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
