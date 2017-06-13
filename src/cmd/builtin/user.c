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
#include "builtin.h"

#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <pwd.h>
#include <argz.h>

typedef struct {
    const char *name;
    uint32_t value;
} role_t;

static role_t roletab[] = {
    { "owner",    FLUX_ROLE_OWNER },
    { "user",     FLUX_ROLE_USER },
    { NULL, 0 },
};

const char *unknown_role_msg = "Valid roles are owner, user.";

static const char *rolestr (uint32_t rolemask, char *s, size_t len)
{
    const char *ret = s;
    role_t *rp = &roletab[0];

    if (rolemask == 0) {
        snprintf (s, len, "none");
        goto done;
    }
    while ((rp->name != NULL)) {
        if ((rolemask & rp->value)) {
            snprintf (s, len, "%s%s", ret < s ? "," : "", rp->name);
            len -= strlen (s);
            s += strlen (s);
        }
        rp++;
    }
done:
    return ret;
}

static int strrole (const char *s, uint32_t *rolemask)
{
    role_t *rp = &roletab[0];

    while ((rp->name != NULL)) {
        if (!strcmp (rp->name, s)) {
            *rolemask |= rp->value;
            return 0;
        }
        rp++;
    }
    return -1;
}

static int parse_rolemask_string (const char *s, uint32_t *rolemask)
{
    char *argz = NULL;
    size_t argz_len;
    int e;
    char *entry  = NULL;
    uint32_t mask = FLUX_ROLE_NONE;
    int rc = -1;

    if ((e = argz_create_sep (s, ',', &argz, &argz_len)) != 0) {
        errno = e;
        goto done;
    }
    while ((entry = argz_next (argz, argz_len, entry))) {
        if (strrole (entry, &mask) < 0)
            goto done;
    }
    *rolemask = mask;
    rc = 0;
done:
    free (argz);
    return rc;
}

static void delrole (flux_t *h, uint32_t userid, uint32_t rolemask)
{
    flux_future_t *f;
    uint32_t final;
    char s[256];

    f = flux_rpcf (h, "userdb.delrole", FLUX_NODEID_ANY, 0,
                   "{s:i s:i}", "userid", userid,
                                "rolemask", rolemask);
    if (!f)
        log_err_exit ("userdb.delrole");
    if (flux_rpc_getf (f, "{s:i s:i}", "userid", &userid,
                                       "rolemask", &final) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("userdb module is not loaded");
        if (errno == ENOENT)
            log_msg_exit ("No such user: %" PRIu32, userid);
        log_err_exit ("userdb.delrole");
    }
    printf ("%" PRIu32 ":%s\n", userid, rolestr (final, s, sizeof (s)));
    flux_future_destroy (f);
}

static void addrole (flux_t *h, uint32_t userid, uint32_t rolemask)
{
    flux_future_t *f;
    uint32_t final;
    char s[256];

    f = flux_rpcf (h, "userdb.addrole", FLUX_NODEID_ANY, 0,
                   "{s:i s:i}", "userid", userid,
                                    "rolemask", rolemask);
    if (!f)
        log_err_exit ("userdb.addrole");
    if (flux_rpc_getf (f, "{s:i s:i}", "userid", &userid,
                                       "rolemask", &final) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("userdb module is not loaded");
        if (errno == ENOENT)
            log_msg_exit ("No such user: %" PRIu32, userid);
        log_err_exit ("userdb.addrole");
    }
    printf ("%" PRIu32 ":%s\n", userid, rolestr (final, s, sizeof (s)));
    flux_future_destroy (f);
}

static uint32_t lookup_user (const char *name)
{
    struct passwd pwd, *result;
    long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
    char *buf;
    int e;
    uint32_t userid = FLUX_USERID_UNKNOWN;

    if (bufsize == -1)
        bufsize = 16384;        /* Should be more than enough */
    buf = xzmalloc (bufsize);
    e = getpwnam_r (name, &pwd, buf, bufsize, &result);
    if (result == NULL) {
        if (e == 0)
            log_msg_exit ("%s: unknown user", name);
        else
            log_errn_exit (e, "%s", name);
    }
    userid = result->pw_uid;
    free (buf);
    return userid;
}

static int internal_user_list (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    flux_future_t *f;
    uint32_t userid;
    uint32_t rolemask;
    char s[256];

    n = optparse_option_index (p);
    if (n != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    for (;;) {
        f = flux_rpc (h, "userdb.getnext", NULL, FLUX_NODEID_ANY, 0);
        if (!f)
            log_err_exit ("userdb.getnext");
        if (flux_rpc_getf (f, "{s:i s:i}", "userid", &userid,
                                           "rolemask", &rolemask) < 0) {
            if (errno == ENOSYS)
                log_msg_exit ("userdb module is not loaded");
            if (errno != ENOENT)
                log_err_exit ("userdb.getnext");
            flux_future_destroy (f);
            break;
        }
        printf ("%" PRIu32 ":%s\n",
                userid, rolestr (rolemask, s, sizeof (s)));

        flux_future_destroy (f);
    }
    flux_close (h);
    return (0);
}

static int internal_user_lookup (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    flux_future_t *f;
    uint32_t userid;
    uint32_t rolemask;
    char *endptr;
    char s[256];

    n = optparse_option_index (p);
    if (n != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    userid = strtoul (av[n], &endptr, 10);
    if (*endptr != '\0')
        userid = lookup_user (av[n]);
    if (userid == FLUX_USERID_UNKNOWN)
        log_msg_exit ("%s: invalid userid", av[n]);
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    f = flux_rpcf (h, "userdb.lookup", FLUX_NODEID_ANY, 0,
                   "{s:i}", "userid", userid);
    if (!f)
        log_err_exit ("userdb.lookup");
    if (flux_rpc_getf (f, "{s:i s:i}", "userid", &userid,
                                       "rolemask", &rolemask) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("userdb module is not loaded");
        if (errno == ENOENT)
            log_msg_exit ("No such user: %" PRIu32, userid);
        log_err_exit ("userdb.lookup");
    }
    printf ("%" PRIu32 ":%s\n",
            userid, rolestr (rolemask, s, sizeof (s)));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_user_addrole (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    uint32_t userid, rolemask;
    char *endptr;

    n = optparse_option_index (p);
    if (n != ac - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    userid = strtoul (av[n], &endptr, 10);
    if (*endptr != '\0')
        userid = lookup_user (av[n]);
    if (userid == FLUX_USERID_UNKNOWN)
        log_msg_exit ("%s: invalid userid", av[n]);
    n++;
    rolemask = strtoul (av[n], &endptr, 0);
    if (*endptr != '\0') {
        if (parse_rolemask_string (av[n], &rolemask) < 0)
            log_err_exit ("%s: invalid rolemask", av[n]);
    }
    if (rolemask == FLUX_ROLE_NONE)
        log_msg_exit ("%s: invalid rolemask", av[n]);
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    addrole (h, userid, rolemask);
    flux_close (h);
    return (0);
}

static int internal_user_delrole (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    uint32_t userid, rolemask;
    char *endptr;

    n = optparse_option_index (p);
    if (n != ac - 2) {
        optparse_print_usage (p);
        exit (1);
    }
    userid = strtoul (av[n], &endptr, 10);
    if (*endptr != '\0')
        userid = lookup_user (av[n]);
    if (userid == FLUX_USERID_UNKNOWN)
        log_msg_exit ("%s: invalid userid", av[n]);
    n++;
    rolemask = strtoul (av[n], &endptr, 0);
    if (*endptr != '\0') {
        if (parse_rolemask_string (av[n], &rolemask) < 0)
            log_err_exit ("%s: invalid rolemask", av[n]);
    }
    if (rolemask == FLUX_ROLE_NONE)
        log_msg_exit ("%s: invalid rolemask", av[n]);
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    delrole (h, userid, rolemask);
    flux_close (h);
    return (0);
}


int cmd_user (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-user");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_subcommand user_subcmds[] = {
    { "list",
      "",
      "List users and their assigned roles",
      internal_user_list,
      0,
      NULL,
    },
    { "lookup",
      "USERID",
      "Lookup roles assigned to USERID",
      internal_user_lookup,
      0,
      NULL,
    },
    { "addrole",
      "USERID role[,role,...]",
      "Add roles to USERID",
      internal_user_addrole,
      0,
      NULL,
    },
    { "delrole",
      "USERID role[,role,...]",
      "Remove roles from USERID",
      internal_user_delrole,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_user_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "user", cmd_user, NULL, "Access user database", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "user"),
                                  user_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
