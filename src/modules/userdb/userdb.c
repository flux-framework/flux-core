/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* userdb.c - map userid to rolemask
 *
 * The instance owner is automatically added with the FLUX_ROLE_OWNER role.
 *
 * If the module is loaded with --default-rolemask=ROLE[,ROLE,...]
 * then new userids are automatically added upon lookup, with the
 * specified roles.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"

#define USERDB_CTX_MAGIC 0x2134aaaa
typedef struct {
    int magic;
    optparse_t *opt;
    uint32_t default_rolemask;
    zhash_t *db;
    zhash_t *iterators;
} userdb_ctx_t;

struct user {
    uint32_t userid;
    uint32_t rolemask;
};

static struct optparse_option opts[] = {
    { .name = "default-rolemask",
      .has_arg = 1,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .arginfo = "ROLE[,ROLE,...]",
      .usage = "Assign specified roles to all users",
    },
    OPTPARSE_TABLE_END,
};

static void freelist (zlist_t *l)
{
    zlist_destroy (&l);
}

static void freectx (void *arg)
{
    userdb_ctx_t *ctx = arg;
    if (ctx) {
        ctx->magic = ~USERDB_CTX_MAGIC;
        optparse_destroy (ctx->opt);
        zhash_destroy (&ctx->db);
        zhash_destroy (&ctx->iterators);
        free (ctx);
    }
}

static userdb_ctx_t *getctx (flux_t *h, int argc, char **argv)
{
    userdb_ctx_t *ctx = (userdb_ctx_t *)flux_aux_get (h, "flux::userdb");
    const char *arg;
    optparse_err_t e;

    if (!ctx) {
        if (!(ctx = calloc (1, sizeof (*ctx)))) {
            errno = ENOMEM;
            goto error;
        }
        ctx->magic = USERDB_CTX_MAGIC;
        ctx->default_rolemask = FLUX_ROLE_NONE;
        if (!(ctx->opt = optparse_create ("userdb"))) {
            errno = ENOMEM;
            goto error;
        }
        e = optparse_add_option_table (ctx->opt, opts);
        if (e != OPTPARSE_SUCCESS) {
            if (e == OPTPARSE_NOMEM)
                errno = ENOMEM;
            else
                errno = EINVAL;
            goto error;
        }
        if (optparse_parse_args (ctx->opt, argc + 1,
                                           argv - 1) < 0) {
            errno = EINVAL;
            goto error;
        }
        optparse_getopt_iterator_reset (ctx->opt, "default-rolemask");
        while ((arg = optparse_getopt_next (ctx->opt, "default-rolemask"))) {
            if (!strcmp (arg, "user"))
                ctx->default_rolemask |= FLUX_ROLE_USER;
            else if (!strcmp (arg, "owner"))
                ctx->default_rolemask |= FLUX_ROLE_OWNER;
            else {
                flux_log (h, LOG_ERR, "unknown role: %s", arg);
                errno = EINVAL;
                goto error;
            }
        }
        if (optparse_hasopt (ctx->opt, "default-rolemask"))
            flux_log (h, LOG_INFO, "default rolemask override=0x%" PRIx32,
                      ctx->default_rolemask);
        if (!(ctx->db = zhash_new ()) || !(ctx->iterators = zhash_new ())) {
            errno = ENOMEM;
            goto error;
        }
        flux_aux_set (h, "flux::userdb", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

struct user *user_create (uint32_t userid, uint32_t rolemask)
{
    struct user *up;

    if (!(up = calloc (1, sizeof (*up)))) {
        errno = ENOMEM;
        goto error;
    }
    up->userid = userid;
    up->rolemask = rolemask;
    return up;
error:
    free (up);
    return NULL;
}

static struct user *user_add (userdb_ctx_t *ctx, uint32_t userid,
                                                 uint32_t rolemask)
{
    struct user *up = NULL;
    char key[16];

    snprintf (key, sizeof (key), "%" PRIu32, userid);
    if (!(up = user_create (userid, rolemask))) {
        errno = ENOMEM;
        goto error;
    }
    if (zhash_insert (ctx->db, key, up) < 0) {
        errno = EEXIST;
        goto error;
    }
    zhash_freefn (ctx->db, key, (zhash_free_fn *)free);
    return up;
error:
    free (up);
    return NULL;
}

static struct user *user_lookup (userdb_ctx_t *ctx, uint32_t userid)
{
    struct user *up = NULL;
    char key[16];

    snprintf (key, sizeof (key), "%" PRIu32, userid);
    if (!(up = zhash_lookup (ctx->db, key))) {
        errno = ENOENT;
        goto error;
    }
    return up;
error:
    free (up);
    return NULL;
}

static void user_delete (userdb_ctx_t *ctx, uint32_t userid)
{
    char key[16];

    snprintf (key, sizeof (key), "%" PRIu32, userid);
    zhash_delete (ctx->db, key);
}

static void lookup (flux_t *h, flux_msg_handler_t *w,
                    const flux_msg_t *msg, void *arg)
{
    userdb_ctx_t *ctx = arg;
    uint32_t userid;
    struct user *up;

    if (flux_request_decodef (msg, NULL, "{s:i}", "userid", &userid) < 0)
        goto error;
    if (!(up = user_lookup (ctx, userid))) {
        if (ctx->default_rolemask != FLUX_ROLE_NONE) {
            if (!(up = user_add (ctx, userid, ctx->default_rolemask)))
                goto error;
        } else
            goto error;
    }
    if (flux_respondf (h, msg, "{s:i s:i}", "userid", up->userid,
                                            "rolemask", up->rolemask) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void addrole (flux_t *h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    userdb_ctx_t *ctx = arg;
    uint32_t userid, rolemask;
    struct user *up;

    if (flux_request_decodef (msg, NULL, "{s:i s:i}",
                              "userid", &userid,
                              "rolemask", &rolemask) < 0)
        goto error;
    if (!(up = user_lookup (ctx, userid))) {
        if (rolemask == FLUX_ROLE_NONE)
            rolemask = ctx->default_rolemask;
        if (rolemask == FLUX_ROLE_NONE) {
            errno = EINVAL;
            goto error;
        }
        if (!(up = user_add (ctx, userid, rolemask)))
            goto error;
    } else
        up->rolemask |= rolemask;
    if (flux_respondf (h, msg, "{s:i s:i}", "userid", up->userid,
                                            "rolemask", up->rolemask) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static void delrole (flux_t *h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    userdb_ctx_t *ctx = arg;
    uint32_t userid, rolemask;
    struct user *up;

    if (flux_request_decodef (msg, NULL, "{s:i s:i}",
                              "userid", &userid,
                              "rolemask", &rolemask) < 0)
        goto error;
    if (!(up = user_lookup (ctx, userid)))
        goto error;
    up->rolemask &= ~rolemask;
    if (flux_respondf (h, msg, "{s:i s:i}", "userid", up->userid,
                                            "rolemask", up->rolemask) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    if (up->rolemask == FLUX_ROLE_NONE)
        user_delete (ctx, userid);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
}

static int compare_keys (const char *s1, const char *s2)
{
    uint32_t u1 = strtoul (s1, NULL, 10);
    uint32_t u2 = strtoul (s2, NULL, 10);
    if (u1 < u2)
        return -1;
    if (u1 > u2)
        return 1;
    return 0;
}

static void getnext (flux_t *h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    userdb_ctx_t *ctx = arg;
    char *key;
    struct user *up = NULL;
    char *uuid = NULL;
    zlist_t *itr;

    if (flux_msg_get_route_first (msg, &uuid) < 0)
        goto error;
    if (!(itr = zhash_lookup (ctx->iterators, uuid))) {
        if (!(itr = zhash_keys (ctx->db))) {
            errno = ENOMEM;
            goto error;
        }
        zlist_sort (itr, (zlist_compare_fn *)compare_keys);
        zhash_update (ctx->iterators, uuid, itr);
        zhash_freefn (ctx->iterators, uuid, (zhash_free_fn *)freelist);
        key = zlist_first (itr);
    } else {
        key = zlist_next (itr);
    }
    if (!key || !(up = zhash_lookup (ctx->db, key))) {
        zhash_delete (ctx->iterators, uuid);
        errno = ENOENT;
        goto error;
    }

    if (flux_respondf (h, msg, "{s:i s:i}", "userid", up->userid,
                                            "rolemask", up->rolemask) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s", __FUNCTION__);
    free (uuid);
}

static void disconnect (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    userdb_ctx_t *ctx = arg;
    char *uuid;
    if (flux_msg_get_route_first (msg, &uuid) == 0) {
        zhash_delete (ctx->iterators, uuid);
        free (uuid);
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "userdb.lookup", lookup},
    { FLUX_MSGTYPE_REQUEST,  "userdb.addrole", addrole },
    { FLUX_MSGTYPE_REQUEST,  "userdb.delrole", delrole },
    { FLUX_MSGTYPE_REQUEST,  "userdb.getnext", getnext},
    { FLUX_MSGTYPE_REQUEST,  "userdb.disconnect", disconnect},
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    userdb_ctx_t *ctx;
    struct user *up;

    if (!(ctx = getctx (h, argc, argv))) {
        goto done;
    }
    if (!(up = user_add (ctx, geteuid (), FLUX_ROLE_OWNER))) {
        flux_log_error (h, "failed to add owner to userdb");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }
    rc = 0;
done_unreg:
    flux_msg_handler_delvec (htab);
done:
    return rc;
}

MOD_NAME ("userdb");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
