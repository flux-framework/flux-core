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
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#include "conf.h"

struct flux_conf_struct {
    char *confdir;
    zconfig_t *z;
};

struct flux_conf_itr_struct {
    flux_conf_t cf;
    zlist_t *zl;
    const char *item;
};

void flux_conf_destroy (flux_conf_t cf)
{
    if (cf->confdir)
        free (cf->confdir);
    if (cf->z)
        zconfig_destroy (&cf->z);
    free (cf);
}

flux_conf_t flux_conf_create (void)
{
    flux_conf_t cf = xzmalloc (sizeof (*cf));
    struct passwd *pw = getpwuid (getuid ());

    if (!pw || !pw->pw_dir || strlen (pw->pw_dir) == 0) {
        free (cf);
        return NULL;
    }
    cf->confdir = xasprintf ("%s/.flux", pw->pw_dir);
    if (!(cf->z = zconfig_new ("root", NULL)))
        oom ();
    return cf;
}

const char *flux_conf_get_directory (flux_conf_t cf)
{
    return cf->confdir;
}

void flux_conf_set_directory (flux_conf_t cf, const char *path)
{
    free (cf->confdir);
    cf->confdir = xstrdup (path);
}

void flux_conf_clear (flux_conf_t cf)
{
    zconfig_destroy (&cf->z);
    if (!(cf->z = zconfig_new ("root", NULL)))
        oom ();
}

int flux_conf_load (flux_conf_t cf)
{
    char *path = xasprintf ("%s/config", cf->confdir);
    int rc = -1;

    if (access (path, R_OK) < 0)
        goto done;
    zconfig_destroy (&cf->z);
    if (!(cf->z = zconfig_load (path))) {
        errno = EINVAL; /* FIXME more appropriate 'parse error' errno? */
        goto done;
    }
    rc = 0;
done:
    free (path);
    return rc;
}

int flux_conf_save (flux_conf_t cf)
{
    struct stat sb;

    if (stat (cf->confdir, &sb) < 0)
        return -1;
    if (!S_ISDIR (sb.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    char *path = xasprintf ("%s/config", cf->confdir);

    zconfig_set_comment (cf->z, NULL);
    zconfig_set_comment (cf->z, " The format of this file is described in");
    zconfig_set_comment (cf->z, "     http://rfc.zeromq.org/spec:4/ZPL");
    zconfig_set_comment (cf->z, " NOTE: indents must be exactly 4 spaces");
    zconfig_set_comment (cf->z, "");

    umask (022);
    if (zconfig_save (cf->z, path) < 0) {
        free (path);
        return -1;
    }
    free (path);
    return 0;
}

static char *xstrsub (const char *str, char a, char b)
{
    char *cpy = xstrdup (str);
    char *s = cpy;
    while (*s) {
        if (*s == a)
            *s = b;
        s++;
    }
    return cpy;
}

const char *flux_conf_get (flux_conf_t cf, const char *key)
{
    char *val = NULL;
    char *nkey = NULL;
    zconfig_t *z;

    if (!key || strchr (key, '/')) {
        errno = EINVAL;
        goto done;
    }
    nkey = xstrsub (key, '.', '/');
    z = zconfig_locate (cf->z, nkey);
    if (!z || !(val = zconfig_value (z)) || strlen (val) == 0) {
        val = NULL;
        errno = ENOENT;
        goto done;
    }
done:
    if (nkey)
        free (nkey);
    return val;
}

int flux_conf_put (flux_conf_t cf, const char *key, const char *fmt, ...)
{
    char *val = NULL;
    char *nkey = NULL;
    va_list ap;
    zconfig_t *z;
    int rc = -1;

    if (!key || strchr (key, '/')) {
        errno = EINVAL;
        goto done;
    }
    nkey = xstrsub (key, '.', '/');
    z = zconfig_locate (cf->z, nkey);
    if (z && zconfig_child (z)) {
        errno = EISDIR;
        goto done;
    }
    if (fmt) {
        va_start (ap, fmt);
        val = xvasprintf (fmt, ap);
        va_end (ap);
    }
    if (z)
        zconfig_set_value (z, val ? "%s" : NULL, val);
    else
        zconfig_put (cf->z, nkey, val);
    rc = 0;
done:
    if (nkey)
        free (nkey);
    if (val)
        free (val);
    return rc;
}

void flux_conf_itr_destroy (flux_conf_itr_t itr)
{
    zlist_destroy (&itr->zl);
    free (itr);
}

static void zconfig_to_zlist (zconfig_t *z, const char *prefix, zlist_t *zl)
{
    z = zconfig_child (z);
    while (z) {
        char *key = xasprintf ("%s%s%s", prefix ? prefix : "",
                                         prefix ? "."    : "",
                                         zconfig_name (z));
        if (zconfig_child (z)) {
            zconfig_to_zlist (z, key, zl);
            free (key);
        } else {
            if (zlist_append (zl, key) < 0)
                oom ();
        }
        z = zconfig_next (z);
    }
}

flux_conf_itr_t flux_conf_itr_create (flux_conf_t cf)
{
    flux_conf_itr_t itr = xzmalloc (sizeof (*itr));
    itr->cf = cf;
    if (!(itr->zl = zlist_new ()))
        oom ();
    zlist_autofree (itr->zl); 
    zconfig_to_zlist (cf->z, NULL, itr->zl);
    itr->item = zlist_first (itr->zl);
    return itr;
}

const char *flux_conf_next (flux_conf_itr_t itr)
{
    const char *item = itr->item;
    if (item)
        itr->item = zlist_next (itr->zl);
    return item;
}

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"

void test_getput (void)
{
    flux_conf_t cf;

    ok (((cf = flux_conf_create ()) != NULL), "created conf");

    ok ((flux_conf_get (cf, "foo") == NULL && errno == ENOENT),
        "get of unknown key returns NULL (errno = ENOENT)");

    ok ((flux_conf_put (cf, "foo", "bar") == 0),
        "set a value for key");
    like (flux_conf_get (cf, "foo"), "^bar$",
        "get returns correct value");

    ok ((flux_conf_put (cf, "foo", "baz") == 0),
        "set a different value for key");
    like (flux_conf_get (cf, "foo"), "^baz$",
        "get returns new value");

    ok ((flux_conf_put (cf, "foo", NULL) == 0),
        "set NULL value for key to delete it");
    ok ((flux_conf_get (cf, "foo") == NULL && errno == ENOENT),
        "get returns NULL (errno = ENOENT)");

    ok ((flux_conf_put (cf, "a.b.c", "42") == 0),
        "set value for hierarchical key");
    like (flux_conf_get (cf, "a.b.c"), "^42$",
        "get returns correct value");
    like (flux_conf_get (cf, ".a.b.c"), "^42$",
        "get with leading path separator returns same value");

    ok ((flux_conf_get (cf, "a.b.c.") == NULL),
        "get with trailing path separator returns NULL (errno = ENONENT)");

    ok ((flux_conf_get (cf, "a.b") == NULL && errno == ENOENT),
        "get of parent 'directory' returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "a") == NULL && errno == ENOENT),
        "get of grandparent 'directory' returns NULL (errno = ENOENT)");

    ok ((flux_conf_get (cf, ".") == NULL && errno == ENOENT),
        "get of . returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "/") == NULL && errno == EINVAL),
        "get of / returns NULL (errno = EINVAL)");
    ok ((flux_conf_get (cf, "root") == NULL && errno == ENOENT),
        "get of 'root' returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "") == NULL && errno == ENOENT),
        "get of '' returns NULL (errno = ENOENT)");

    ok ((flux_conf_get (cf, NULL) == NULL && errno == EINVAL),
        "get of NULL key returns NULL (errno = EINVAL)");
    ok ((flux_conf_put (cf, NULL, NULL) == -1 && errno == EINVAL),
        "put get of NULL key returns -1 (errno = EINVAL)");


    ok ((flux_conf_put (cf, "a.b.x", "43") == 0),
        "set value for secnod hierarchical key");
    like (flux_conf_get (cf, "a.b.x"), "^43$",
        "get returns correct value");

    ok ((flux_conf_put (cf, "a", NULL) == -1 && errno == EISDIR),
        "put NULL on grandparent directory returns -1 (errno = EISDIR)");
    like (flux_conf_get (cf, "a.b.c"), "^42$",
        "get of first hier key returns correct value");
    like (flux_conf_get (cf, "a.b.x"), "^43$",
        "get of second hier key returns correct value");

    flux_conf_destroy (cf);
}

void test_iterator (void)
{
    flux_conf_t cf;
    flux_conf_itr_t itr;

    ok (((cf = flux_conf_create ()) != NULL), "created conf");
    ok ((flux_conf_put (cf, "a", "x") == 0), "added first item");
    ok ((flux_conf_put (cf, "b.m.y", "y") == 0), "added second item");
    ok ((flux_conf_put (cf, "c.x", "z") == 0), "added third item");
    ok ((flux_conf_put (cf, "c.y", "Z") == 0), "added last item");

    ok (((itr = flux_conf_itr_create (cf)) != NULL), "created itr");
    like (flux_conf_next (itr), "^a$", "itr returned first item");
    like (flux_conf_next (itr), "^b.m.y$", "itr returned second item");
    like (flux_conf_next (itr), "^c.x$", "itr returned third item");
    like (flux_conf_next (itr), "^c.y$", "itr returned last item");
    ok ((flux_conf_next (itr) == NULL), "itr returned NULL");

    flux_conf_itr_destroy (itr);
    flux_conf_destroy (cf);
}

int main (int argc, char *argv[])
{
    plan (36);

    test_getput (); /* 25 tests */
    test_iterator (); /* 11 tests */

    done_testing ();
    return (0);
}
#endif /* TEST_MAIN */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
