/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "proto.h"

JSON modctl_tunload_enc (const char *name)
{
    JSON o = NULL;

    if (!name) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "name", name);
done:
    return o;
}

int modctl_tunload_dec (JSON o, const char **name)
{
    int rc = -1;

    if (!o || !name) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "name", name)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_runload_enc (int errnum)
{
    JSON o = NULL;

    o = Jnew ();
    Jadd_int (o, "errnum", errnum);
    return o;
}

int modctl_runload_dec (JSON o, int *errnum)
{
    int rc = -1;

    if (!o || !errnum) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}


JSON modctl_tload_enc (const char *path, int argc, char **argv)
{
    JSON args, o = NULL;
    int i;

    if (!path) {
        errno = EINVAL;
        goto done;
    }
    for (i = 0; i < argc; i++) {
        if (argv[i] == NULL) {
            errno = EINVAL;
            goto done;
        }
    }
    o = Jnew ();
    Jadd_str (o, "path", path);
    args = Jnew_ar ();
    for (i = 0; i < argc; i++)
        Jadd_ar_str (args, argv[i]);
    Jadd_obj (o, "args", args);
done:
    return o;
}

int modctl_tload_dec (JSON o, const char **path, int *argc, const char ***argv)
{
    JSON args = NULL;
    int i, ac, rc = -1;
    const char **av;

    if (!o || !path || !argc || !argv) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "path", path) || !Jget_obj (o, "args", &args)
                                    || !Jget_ar_len (args, &ac)) {
        errno = EPROTO;
        goto done;
    }
    av = xzmalloc (sizeof (av[0]) * ac);
    for (i = 0; i < ac; i++) {
        if (!Jget_ar_str (args, i, &av[i])) {
            free (av);
            errno = EPROTO;
            goto done;
        }
    }
    *argc = ac;
    *argv = av;
    rc = 0;
done:
    return rc;
}

JSON modctl_rload_enc (int errnum)
{
    JSON o = Jnew ();
    Jadd_int (o, "errnum", errnum);
    return o;
}

int modctl_rload_dec (JSON o, int *errnum)
{
    int rc = -1;

    if (!o || !errnum) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_tlist_enc (const char *svc)
{
    JSON o = NULL;

    if (!svc) {
        errno = EINVAL;
        goto done;
    }
    o = Jnew ();
    Jadd_str (o, "service", svc);
done:
    return o;
}

int modctl_tlist_dec (JSON o, const char **svc)
{
    int rc = -1;

    if (!o || !svc) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_str (o, "service", svc)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

JSON modctl_rlist_enc (void)
{
    JSON mods, o = Jnew ();
    mods = Jnew_ar ();
    Jadd_obj (o, "modules", mods);
    return o;
}

int modctl_rlist_enc_add (JSON o, const char *name, int size,
                          const char *digest, int idle)
{
    JSON mods, mod;
    int rc = -1;

    if (!o || !name || !digest || size < 0 || idle < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "modules", &mods)) { /* does not take ref on 'mods' */
        errno = EINVAL;
        goto done;
    }
    mod = Jnew ();
    Jadd_str (mod, "name", name);
    Jadd_int (mod, "size", size);
    Jadd_str (mod, "digest", digest);
    Jadd_int (mod, "idle", idle);
    Jadd_ar_obj (mods, mod); /* takes ref on mod */
    Jput (mod);
    rc = 0;
done:
    return rc;
}

int modctl_rlist_enc_errnum (JSON o, int errnum)
{
    Jadd_int (o, "errnum", errnum);
    return 0;
}

int modctl_rlist_dec (JSON o, int *errnum, int *len)
{
    int rc = -1;
    JSON mods;

    if (!o || !errnum || !len) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_int (o, "errnum", errnum) || !Jget_obj (o, "modules", &mods)
                                        || !Jget_ar_len (mods, len)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int modctl_rlist_dec_nth (JSON o, int n, const char **name,
                          int *size, const char **digest, int *idle)
{
    JSON el, mods;
    int rc = -1;
    int len;

    if (!o || !name || !size || !digest || !idle) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_obj (o, "modules", &mods) || !Jget_ar_len (mods, &len)) {
        errno = EPROTO;
        goto done;
    }
    if (n < 0 || n > len) {
        errno = EINVAL;
        goto done;
    }
    if (!Jget_ar_obj (mods, n, &el)) { /* does not take ref on el */
        errno = EPROTO;
        goto done;
    }
    if (!Jget_str (el, "name", name) || !Jget_int (el, "size", size)
        || !Jget_str (el, "digest", digest) || !Jget_int (el, "idle", idle)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"

void test_tload (void)
{
    JSON o;
    char *av[] = { "a", "b", "c" };
    const char *path;
    int argc;
    const char **argv;

    o = modctl_tload_enc ("/foo/bar.so", 3, av);
    ok (o != NULL,
        "modctl_tload_enc works");
    ok (modctl_tload_dec (o, &path, &argc, &argv) == 0 && path && argc == 3,
        "modctl_tload_dec works");
    like (path, "^/foo/bar.so$",
        "modctl_tload_dec returned encoded path");
    like (argv[0], "^a$",
        "modctl_tload_dec returned encoded argv[0]");
    like (argv[1], "^b$",
        "modctl_tload_dec returned encoded argv[1]");
    like (argv[2], "^c$",
        "modctl_tload_dec returned encoded argv[2]");
    free (argv);
    Jput (o);
}

void test_rload (void)
{
    JSON o;
    int errnum;

    o = modctl_rload_enc (42);
    ok (o != NULL,
        "modctl_rload_enc works");
    ok (modctl_rload_dec (o, &errnum) == 0,
        "modctl_rload_dec works");
    ok (errnum == 42,
        "modctl_rload_dec returns encoded errnum");
    Jput (o);

}

void test_tunload (void)
{
    JSON o;
    const char *name = NULL;

    o = modctl_tunload_enc ("bar");
    ok (o != NULL,
        "modctl_tunload_enc works");
    ok (modctl_tunload_dec (o, &name) == 0 && name,
        "modctl_tunload_dec works");
    like (name, "^bar$",
        "modctl_tunload_dec returned encoded module name");
    Jput (o);
}

void test_runload (void)
{
    JSON o;
    int errnum;

    o = modctl_runload_enc (42);
    ok (o != NULL,
        "modctl_runload_enc works");
    ok (modctl_runload_dec (o, &errnum) == 0,
        "modctl_runload_dec works");
    ok (errnum == 42,
        "modctl_runload_dec returns encoded errnum");
    Jput (o);

}

void test_tlist (void)
{
    JSON o;
    const char *svc;

    o = modctl_tlist_enc ("foo");
    ok (o != NULL,
        "modctl_tlist_enc works");
    ok (modctl_tlist_dec (o, &svc) == 0 && svc,
        "modctl_tlist_dec works");
    like (svc, "^foo$",
        "modctl_tlist_dec returned encoded service");
    Jput (o);
}

void test_rlist (void)
{
    JSON o;
    const char *name, *digest;
    int len, errnum, size, idle;

    o = modctl_rlist_enc ();
    ok (o != NULL,
        "modctl_rlist_enc works");
    ok (modctl_rlist_enc_add (o, "foo", 42, "abba", 6) == 0,
        "modctl_rlist_enc_add works 0th time");
    ok (modctl_rlist_enc_add (o, "bar", 69, "argh", 19) == 0,
        "modctl_rlist_enc_add works 1st time");
    ok (modctl_rlist_enc_errnum (o, 0) == 0,
        "modctl_rlist_enc_errnum works");
    ok (modctl_rlist_dec (o, &errnum, &len) == 0 && errnum == 0 && len == 2,
        "modctl_rlist_dec works");
    ok (modctl_rlist_dec_nth (o, 0, &name, &size, &digest, &idle) == 0
        && name && size == 42 && digest && idle == 6,
        "modctl_rlist_dec_nth(0) works and returns correct scalar vals");
    like (name, "^foo$",
        "modctl_rlist_dec_nth(0) returns encoded name");
    like (digest, "^abba$",
        "modctl_rlist_dec_nth(0) returns encoded digest");
    ok (modctl_rlist_dec_nth (o, 1, &name, &size, &digest, &idle) == 0
        && name && size == 69 && digest && idle == 19,
        "modctl_rlist_dec_nth(1) works and returns correct scalar vals");
    like (name, "^bar$",
        "modctl_rlist_dec_nth(1) returns encoded name");
    like (digest, "^argh$",
        "modctl_rlist_dec_nth(1) returns encoded digest");

    Jput (o);
}

int main (int argc, char *argv[])
{

    plan (29);

    test_tunload (); // 3
    test_runload (); // 3

    test_tload (); // 6
    test_rload (); // 3

    test_tlist (); // 3
    test_rlist (); // 11

    done_testing ();
    return (0);
}

#endif /* TEST_MAIN */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
