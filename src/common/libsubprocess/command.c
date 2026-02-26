/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#ifdef HAVE_ENVZ_ADD
#include <envz.h>
#else
#include "src/common/libmissing/envz.h"
#endif
#include <assert.h>
#include <fnmatch.h>

#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "command_private.h"
#include "command.h"

struct flux_command {
    char *label;

    char *cwd;

    /* Command arguments in argz format */
    size_t argz_len;
    char *argz;

    /* Command environment hash */
    size_t envz_len;
    char *envz;

    /* Extra key=value options */
    zhash_t *opts;

    /* Extra channels to create in the subprocess (i.e. socketpairs) */
    zlist_t *channels;

    /* Optional message channels (list of 'struct cmd_msgchan') */
    zlist_t *msgchans;
};

/*
 *  Static functions:
 */

static void cmd_msgchan_destroy (struct cmd_msgchan *cm)
{
    if (cm) {
        int saved_errno = errno;
        free (cm->uri);
        free (cm->name);
        free (cm);
        errno = saved_errno;
    }
}

static struct cmd_msgchan *cmd_msgchan_create (const char *name,
                                               const char *uri)
{
    struct cmd_msgchan *cm;
    if (!(cm = calloc (1, sizeof (*cm))))
        return NULL;
    if (!(cm->uri = strdup (uri)) || !(cm->name = strdup (name)))
        goto error;
    return cm;
error:
    cmd_msgchan_destroy (cm);
    return NULL;
}

/*
 *  Initialize an argz vector. If av == NULL then the argz vector is
 *   freed and length (*argz_lenp) reset to 0, otherwise av is passed
 *   directly to argz_create(3).
 */
static int init_argz (char **argzp, size_t *argz_lenp, char * const av[])
{
    int e;
    if (*argzp != NULL) {
        free (*argzp);
        *argzp = NULL;
        *argz_lenp = 0;
    }
    if (av && (e = argz_create (av, argzp, argz_lenp)) != 0) {
        errno = e;
        return -1;
    }
    return (0);
}

/*
 *  Same as init_argz, but pass argument count (ac) and verify that
 *   the argument vector av has NULL as its final element.
 */
static int init_argz_count (char **argzp,
                            size_t *argz_lenp,
                            int ac,
                            char * const av[])
{
    if (av && (av[ac] != NULL)) {
        errno = EINVAL;
        return -1;
    }
    return init_argz (argzp, argz_lenp, av);
}

/*
 *  Append string defined by [fmt, ap] to argz vector in argzp
 */
static int argz_appendv (char **argzp,
                         size_t *argz_lenp,
                         const char *fmt,
                         va_list ap)
{
    int e;
    char *s;
    if (vasprintf (&s, fmt, ap) < 0)
        return -1;
    if ((e = argz_add (argzp, argz_lenp, s))) {
        errno = e;
        return -1;
    }
    free (s);
    return 0;
}

/*
 *  Return an argv/env array for argz/envz object. Caller must free
 *   the returned array, which is filled via argz_extract(3).
 */
static char **expand_argz (char *argz, size_t argz_len)
{
    size_t len;
    char **argv;

    len = argz_count (argz, argz_len) + 1;
    argv = calloc (len + 1, sizeof (char *));
    if (!argv)
        return NULL;

    argz_extract (argz, argz_len, argv);

    return (argv);
}

/*
 *  Return the "name" portion of an environment entry of the form
 *   "NAME=VALUE", copying the name into the destination buffer `dst`
 *   of size `len`. If the result cannot fit in `dst`, NULL is returned
 *   as opposed to truncation.
 *
 *  On success, a pointer to `dst` is returned.
 */
static char *env_entry_name (char *entry, char *dst, size_t len)
{
    const char *p;
    if (!entry)
        return NULL;
    /* If there is no '=' in entry, then "name" is the entire entry. */
    if (!(p = strchr (entry, '=')))
        p = entry + strlen (entry) + 1;

    /* Refuse to truncate */
    if (len-1 < p - entry)
        return NULL;

    /* strncat(3): safer than strncpy(3), faster than by-hand: */
    *dst = '\0';
    return strncat (dst, entry, p - entry);
}

/*
 *  Return a pointer to the "value" portion of an environment entry
 *   of the form "NAME=VALUE". The result should not be modified as
 *   it points to a substring of the original `entry`.
 *
 *  If `entry` does not contain an '=' character, then it has no value
 *   and NULL is returned.
 */
static const char * env_entry_value (const char *entry)
{
    const char *p;
    if (!entry || !(p = strchr (entry, '=')))
        return NULL;
    return p+1;
}

static json_t * argz_tojson (const char *argz, size_t argz_len)
{
    char *arg = NULL;
    json_t *o = json_array ();

    if (o == NULL)
        goto err;

    while ((arg = argz_next (argz, argz_len, arg))) {
        json_t *val = json_string (arg);
        if (!val || json_array_append_new (o, val)) {
            json_decref (val);
            goto err;
        }
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

static int argz_fromjson (json_t *o, char **argzp, size_t *argz_lenp)
{
    size_t index;
    json_t *value;

    assert (*argzp == NULL && *argz_lenp == 0);
    if (!json_is_array (o))
        goto fail;

    json_array_foreach (o, index, value) {
        if (!json_is_string (value))
            goto fail;
        if (argz_add (argzp, argz_lenp, json_string_value (value)))
            goto fail;
    }
    return 0;
fail:
    free (*argzp);
    *argzp = NULL;
    *argz_lenp = 0;
    errno = EINVAL;
    return -1;
}

/*
 *  Convert and envz array (argz with NAME=VALUE entries) to a json
 *   dictionary object.
 */
static json_t * envz_tojson (const char *envz, size_t envz_len)
{
    char buf [1024];
    const char *name, *value;
    char *entry = NULL;
    json_t *o = json_object ();

    if (o == NULL)
        goto err;

    while ((entry = argz_next (envz, envz_len, entry))) {
        json_t *v;
        if (!(name = env_entry_name (entry, buf, sizeof (buf))))
            continue;
        if (!(value = env_entry_value (entry)))
            continue;
        if (!(v = json_string (value)) || json_object_set_new (o, name, v)) {
            json_decref (v);
            goto err;
        }
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

static int envz_fromjson (json_t *o, char **envzp, size_t *envz_lenp)
{
    const char *var;
    json_t *val;
    int errnum = EINVAL;

    assert (*envzp == NULL && *envz_lenp == 0);
    if (!json_is_object (o))
        goto fail;

    json_object_foreach (o, var, val) {
        if (!json_is_string (val))
            goto fail;
        if (envz_add (envzp, envz_lenp, var, json_string_value (val)))
            goto fail;
    }
    return 0;
fail:
    free (*envzp);
    *envzp = NULL;
    *envz_lenp = 0;
    errno = errnum;
    return -1;
}

/*
 *  Convert a hash with string keys,values to json string
 */
static json_t * zhash_tojson (zhash_t *h)
{
    const char *val;
    json_t *o = json_object ();

    if (o == NULL)
        goto err;

    val = zhash_first (h);
    while (val) {
        json_t *v = json_string (val);
        if (!v || json_object_set_new (o, zhash_cursor (h), v)) {
            json_decref (v);
            goto err;
        }
        val = zhash_next (h);
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

/*
 *  New zhash with string keys/vals from json dictionary `o`.
 *  "autofree" will be set on the hash.
 */
static zhash_t *zhash_fromjson (json_t *o)
{
    const char *key;
    json_t *val;
    zhash_t *h = NULL;
    int errnum = EPROTO;

    if (!json_is_object (o))
        goto fail;

    h = zhash_new ();
    zhash_autofree (h);

    json_object_foreach (o, key, val) {
        if (!json_is_string (val))
            goto fail;
        if (zhash_insert (h, key, (char *) json_string_value (val)) < 0) {
            /* Duplicate key. This can't happen unless json object is
             *  corrupt, so give up and return error (EPROTO)
             */
            goto fail;
        }
    }
    return h;
fail:
    if (h)
        zhash_destroy (&h);
    errno = errnum;
    return NULL;
}

static zlist_t *channels_fromjson (json_t *o)
{
    int errnum = EPROTO;
    size_t index;
    json_t *value;
    zlist_t *l = NULL;

    if (!json_is_array (o))
        goto fail;
    l = zlist_new ();
    zlist_autofree (l);

    json_array_foreach (o, index, value) {
        if (!json_is_string (value))
            goto fail;
        if (zlist_append (l, (char *) json_string_value (value)) < 0) {
            errnum = errno;
            goto fail;
        }
    }
    return l;
fail:
    zlist_destroy (&l);
    errno = errnum;
    return NULL;
}

static json_t *channels_tojson (zlist_t *l)
{
    char *s = NULL;
    json_t *o = json_array ();

    if (o == NULL)
        goto err;

    s = zlist_first (l);
    while (s) {
        json_t *val = json_string (s);
        if (!val || json_array_append_new (o, val)) {
            json_decref (val);
            goto err;
        }
        s = zlist_next (l);
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

static const char *channels_find (zlist_t *l, const char *s)
{
    const char *v = zlist_first (l);
    while (v != NULL) {
        if (streq (s, v))
            return (v);
        v = zlist_next (l);
    }
    return NULL;
}

/* This was added after the original design so if 'o' is
 * NULL (not present in object), return an empty list
 * rather than an error.
 */
static zlist_t *msgchans_fromjson (json_t *o)
{
    size_t index;
    json_t *entry;
    zlist_t *l;

    if (!(l = zlist_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (o) {
        if (!json_is_array (o))
            goto inval;
        json_array_foreach (o, index, entry) {
            const char *name;
            const char *uri;
            struct cmd_msgchan *cm;

            if (json_unpack (entry, "{s:s s:s}", "name", &name, "uri", &uri) < 0)
                goto inval;
            if (!(cm = cmd_msgchan_create (name, uri))
                || zlist_append (l, cm) < 0) {
                cmd_msgchan_destroy (cm);
                zlist_destroy (&l);
                errno = ENOMEM;
                return NULL;
            }
            zlist_freefn (l, cm, (zlist_free_fn *)cmd_msgchan_destroy, true);
        }
    }
    return l;
inval:
    zlist_destroy (&l);
    errno = EINVAL;
    return NULL;
}

static json_t *msgchans_tojson (zlist_t *l)
{
    struct cmd_msgchan *cm;
    json_t *o = json_array ();

    if (o == NULL)
        goto err;

    cm = zlist_first (l);
    while (cm) {
        json_t *val = json_pack ("{s:s s:s}", "name", cm->name, "uri", cm->uri);
        if (!val || json_array_append_new (o, val) < 0) {
            json_decref (val);
            goto err;
        }
        cm = zlist_next (l);
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

static struct cmd_msgchan *msgchans_find (zlist_t *l, const char *s)
{
    struct cmd_msgchan *cm = zlist_first (l);
    while (cm != NULL) {
        if (streq (s, cm->name))
            return cm;
        cm = zlist_next (l);
    }
    return NULL;
}

static zlist_t *msgchans_dup (zlist_t *l)
{
    json_t *o;
    zlist_t *cpy;
    if (!(o = msgchans_tojson (l)))
        return NULL;
    cpy = msgchans_fromjson (o);
    ERRNO_SAFE_WRAP (json_decref, o);
    return cpy;
}

/*  Version of zhash_dup() that duplicates both string keys and values
 */
static zhash_t * z_hash_dup (zhash_t *src)
{
    zhash_t *new;
    zlist_t *keys = zhash_keys (src);
    const char *k;

    new = zhash_new ();
    zhash_autofree (new);

    k = zlist_first (keys);
    while (k) {
        zhash_insert (new, k, zhash_lookup (src, k));
        k = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return (new);
}

/***************************************************************************/
/*
 *  flux_cmd_t interface
 */
static void flux_cmd_free (flux_cmd_t *cmd)
{
    if (cmd) {
        int saved_errno = errno;
        free (cmd->cwd);
        free (cmd->label);
        free (cmd->argz);
        free (cmd->envz);
        zhash_destroy (&cmd->opts);
        zlist_destroy (&cmd->channels);
        zlist_destroy (&cmd->msgchans);
        free (cmd);
        errno = saved_errno;
    }
}

void flux_cmd_destroy (flux_cmd_t *cmd)
{
    flux_cmd_free (cmd);
}

flux_cmd_t *flux_cmd_create (int argc, char *argv[], char **env)
{
    flux_cmd_t *cmd = calloc (1, sizeof (*cmd));

    if (!cmd)
        return NULL;
    if (argv && init_argz_count (&cmd->argz, &cmd->argz_len, argc, argv) < 0)
        goto fail;
    if (env && init_argz (&cmd->envz, &cmd->envz_len, env) < 0)
        goto fail;

    if (!(cmd->opts = zhash_new ())
       || !(cmd->channels = zlist_new ())
       || !(cmd->msgchans = zlist_new ())) {
        errno = ENOMEM;
        goto fail;
    }

    /* Set autofree on both the opts hash and the channels list.
     *
     * This means keys in the hash and items in the list will automatically
     *  be strdup'd on insertion, and freed on destruction. For zlist
     *  it also makes zlist_dup() duplicate values instead of referencing
     *  the originals.
     */
    zhash_autofree (cmd->opts);
    zlist_autofree (cmd->channels);

    return (cmd);
fail:
    flux_cmd_free (cmd);
    return NULL;
}

int flux_cmd_argc (const flux_cmd_t *cmd)
{
    return argz_count (cmd->argz, cmd->argz_len);
}

char *flux_cmd_stringify (const flux_cmd_t *cmd)
{
    if (cmd->argz_len == 0)
        return strdup ("");

    char *result;
    if (!(result = malloc (cmd->argz_len)))
        return NULL;
    memcpy (result, cmd->argz, cmd->argz_len);
    argz_stringify (result, cmd->argz_len, ' ');
    return result;
}

const char *flux_cmd_arg (const flux_cmd_t *cmd, int n)
{
    char *arg = NULL;
    int argc;
    int i;

    argc = flux_cmd_argc (cmd);
    if (n >= argc) {
        errno = EINVAL;
        return NULL;
    }

    for (i = 0; i <= n; i++)
        arg = argz_next (cmd->argz, cmd->argz_len, arg);

    return arg;
}

int flux_cmd_argv_insert (flux_cmd_t *cmd, int n, const char *entry)
{
    const char *arg = flux_cmd_arg (cmd, n);

    /*  Always allow n == 0, even if flux_cmd_arg (cmd, 0) returned NULL.
     *  This means the argv is currently empty and insert is equivalent
     *   to append. (argz_insert with entry == NULL is an implicit append)
     */
    if (arg == NULL && n > 0)
        return -1;

    if (argz_insert (&cmd->argz, &cmd->argz_len, (char *)arg, entry) != 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int flux_cmd_argv_delete (flux_cmd_t *cmd, int n)
{
    const char *arg = flux_cmd_arg (cmd, n);
    if (arg == NULL)
        return -1;

    argz_delete (&cmd->argz, &cmd->argz_len, (char *) arg);
    return 0;
}

int flux_cmd_argv_appendf (flux_cmd_t *cmd, const char *fmt, ...)
{
    int rc = 0;
    int errnum = 0;
    va_list ap;
    va_start (ap, fmt);
    if ((rc = argz_appendv (&cmd->argz, &cmd->argz_len, fmt, ap)) < 0)
        errnum = errno;
    va_end (ap);
    errno = errnum;
    return (rc);
}

int flux_cmd_argv_append (flux_cmd_t *cmd, const char *arg)
{
    int e;
    if (arg == NULL) {
        errno = EINVAL;
        return -1;
    }
    e = argz_add (&cmd->argz, &cmd->argz_len, arg);
    if (e) {
        errno = e;
        return -1;
    }
    return 0;
}

static int flux_cmd_setenv (flux_cmd_t *cmd,
                            const char *k,
                            const char *v,
                            int overwrite)
{
    if (overwrite || !envz_entry (cmd->envz, cmd->envz_len, k)) {
        if (envz_add (&cmd->envz, &cmd->envz_len, k, v) != 0) {
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

int flux_cmd_setenvf (flux_cmd_t *cmd,
                      int overwrite,
                      const char *name,
                      const char *fmt, ...)
{
    va_list ap;
    char *val;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return rc;
    rc = flux_cmd_setenv (cmd, name, val, overwrite);
    free (val);
    return (rc);
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?') || strchr (s, '['))
        return true;
    return false;
}

void flux_cmd_unsetenv (flux_cmd_t *cmd, const char *name)
{
    if (!cmd
        || !name
        || cmd->envz == NULL
        || cmd->envz_len == 0)
        return;

    if (isa_glob (name)) {
        char *cpy = NULL;
        size_t cpy_len = 0;
        char buf[1024];
        char *s;

        if (argz_append (&cpy, &cpy_len, cmd->envz, cmd->envz_len) == 0) {
            char *entry = NULL;
            while ((entry = argz_next (cpy, cpy_len, entry))) {
                if ((s = env_entry_name (entry, buf, sizeof (buf)))
                    && fnmatch (name, s, 0) == 0)
                    envz_remove (&cmd->envz, &cmd->envz_len, s);
            }
            free (cpy);
        }
    }
    else
        envz_remove (&cmd->envz, &cmd->envz_len, name);
}

const char * flux_cmd_getenv (const flux_cmd_t *cmd, const char *name)
{
    return (envz_get (cmd->envz, cmd->envz_len, name));
}

int flux_cmd_setcwd (flux_cmd_t *cmd, const char *path)
{
    free (cmd->cwd);
    cmd->cwd = strdup (path);
    if (cmd->cwd == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

const char * flux_cmd_getcwd (const flux_cmd_t *cmd)
{
    return cmd->cwd;
}

int flux_cmd_add_channel (flux_cmd_t *cmd, const char *name)
{
    if (name == NULL)
        return -1;
    if (channels_find (cmd->channels, name)) {
        errno = EEXIST;
        return -1;
    }
    /* autofree is set on cmd->channels, so name is automatically strdup'd */
    return zlist_append (cmd->channels, (char *)name);
}

int flux_cmd_add_message_channel (flux_cmd_t *cmd,
                                  const char *name,
                                  const char *uri)
{
    struct cmd_msgchan *cm;
    if (!cmd || !name || !uri) {
        errno = EINVAL;
        return -1;
    }
    if (msgchans_find (cmd->msgchans, name)) {
        errno = EEXIST;
        return -1;
    }
    if (!(cm = cmd_msgchan_create (name, uri))
        || zlist_append (cmd->msgchans, cm) < 0) {
        cmd_msgchan_destroy (cm);
        return -1;
    }
    zlist_freefn (cmd->msgchans,
                  cm,
                  (zlist_free_fn *)cmd_msgchan_destroy,
                  true);
    return 0;
}

int flux_cmd_setopt (flux_cmd_t *cmd, const char *var, const char *val)
{
    if (!var || !val) {
        errno = EINVAL;
        return -1;
    }
    /* autofree is set on cmd->opts, so val is automatically strdup'd */
    zhash_update (cmd->opts, var, (char *)val);
    return 0;
}

const char *flux_cmd_getopt (flux_cmd_t *cmd, const char *var)
{
    return zhash_lookup (cmd->opts, var);
}

int flux_cmd_set_label (flux_cmd_t *cmd, const char *label)
{
    char *next = NULL;

    if (!cmd || (label && strlen (label) == 0)) {
        errno = EINVAL;
        return -1;
    }

    if (label && !(next = strdup (label)))
        return -1;
    free (cmd->label);
    cmd->label = next;

    return 0;
}

const char *flux_cmd_get_label (flux_cmd_t *cmd)
{
    if (cmd)
        return cmd->label;
    return NULL;
}

flux_cmd_t * flux_cmd_copy (const flux_cmd_t *src)
{
    error_t e = 0;
    flux_cmd_t *cmd = calloc (1, sizeof (*cmd));
    if (cmd == NULL)
        goto err;
    e = argz_append (&cmd->argz, &cmd->argz_len, src->argz, src->argz_len);
    if (e != 0)
        goto err;
    e = argz_append (&cmd->envz, &cmd->envz_len, src->envz, src->envz_len);
    if (e != 0)
        goto err;
    if (src->cwd && !(cmd->cwd = strdup (src->cwd)))
        goto err;
    if (src->label && !(cmd->label = strdup (src->label)))
        goto err;
    if (!(cmd->msgchans = msgchans_dup (src->msgchans)))
        goto err;
    cmd->channels = zlist_dup (src->channels);
    cmd->opts = z_hash_dup (src->opts);
    return (cmd);
err:
    flux_cmd_destroy (cmd);
    return NULL;
}

flux_cmd_t *cmd_fromjson (json_t *o, json_error_t *errp)
{
    int errnum;
    json_t *jenv = NULL;
    json_t *jargv = NULL;
    json_t *jopts = NULL;
    json_t *jchans = NULL;
    json_t *jmsgchans = NULL;
    const char *cwd = NULL;
    const char *label = NULL;
    flux_cmd_t *cmd = NULL;;

    if (!(cmd = calloc (1, sizeof (*cmd)))) {
        errnum = ENOMEM;
        goto fail;
    }
    if (json_unpack_ex (o,
                        errp,
                        0,
                        "{s?s s?s s:o s:o s:o s:o s?o}",
                        "label", &label,
                        "cwd", &cwd,
                        "cmdline", &jargv,
                        "env", &jenv,
                        "opts", &jopts,
                        "channels", &jchans,
                        "msgchan", &jmsgchans) < 0) {
        errnum = EPROTO;
        goto fail;
    }
    if ((label && !(cmd->label = strdup (label)))
        || (cwd && !(cmd->cwd = strdup (cwd)))
        || (argz_fromjson (jargv, &cmd->argz, &cmd->argz_len) < 0)
        || (envz_fromjson (jenv, &cmd->envz, &cmd->envz_len) < 0)
        || !(cmd->opts = zhash_fromjson (jopts))
        || !(cmd->channels = channels_fromjson (jchans))
        || !(cmd->msgchans = msgchans_fromjson (jmsgchans))) {
        errnum = errno;
        goto fail;
    }
    /* All sub-objects of `o` inherit reference from root object so
     *  this decref should free jenv, jargv, ... etc.
     */
    return cmd;

fail:
    flux_cmd_destroy (cmd);
    errno = errnum;
    return NULL;
}

json_t *cmd_tojson (const flux_cmd_t *cmd)
{
    json_t *o = json_object ();
    json_t *a;

    /* Pack cwd */
    if (cmd->cwd) {
        if (!(a = json_string (cmd->cwd)))
            goto err;
        if (json_object_set_new (o, "cwd", a) != 0) {
            json_decref (a);
            goto err;
        }
    }

    /* Pack label */
    if (cmd->label) {
        if (!(a = json_string (cmd->label)))
            goto err;
        if (json_object_set_new (o, "label", a) != 0) {
            json_decref (a);
            goto err;
        }
    }

    /* Pack argv */
    if (cmd->argz) {
        if (!(a = argz_tojson (cmd->argz, cmd->argz_len)))
            goto err;
        if (json_object_set_new (o, "cmdline", a) != 0) {
            json_decref (a);
            goto err;
        }
    }

    /* Pack env */
    if (cmd->envz) {
        if (!(a = envz_tojson (cmd->envz, cmd->envz_len)))
            goto err;
        if (json_object_set_new (o, "env", a) != 0) {
            json_decref (a);
            goto err;
        }
    }

    /* Pack opts dictionary */
    if (!(a = zhash_tojson (cmd->opts)))
        goto err;
    if (json_object_set_new (o, "opts", a) != 0) {
        json_decref (a);
        goto err;
    }

    /* Pack channels */
    if (!(a = channels_tojson (cmd->channels)))
        goto err;
    if (json_object_set_new (o, "channels", a) != 0) {
        json_decref (a);
        goto err;
    }

    /* Pack msgchan */
    if (!(a = msgchans_tojson (cmd->msgchans)))
        goto err;
    if (json_object_set_new (o, "msgchan", a) != 0) {
        json_decref (a);
        goto err;
    }
    return o;
err:
    json_decref (o);
    return NULL;
}

char **cmd_env_expand (flux_cmd_t *cmd)
{
    return expand_argz (cmd->envz, cmd->envz_len);
}

char **cmd_argv_expand (flux_cmd_t *cmd)
{
    return expand_argz (cmd->argz, cmd->argz_len);
}

int flux_cmd_env_replace (flux_cmd_t *cmd, char **env)
{
    size_t new_envz_len = 0;
    char *new_envz = NULL;

    if (init_argz (&new_envz, &new_envz_len, env) < 0)
        return -1;

    if (cmd->envz)
        free (cmd->envz);
    cmd->envz = new_envz;
    cmd->envz_len = new_envz_len;

    return 0;
}

zlist_t *cmd_channel_list (flux_cmd_t *cmd)
{
    return cmd->channels;
}

zlist_t *cmd_msgchan_list (flux_cmd_t *cmd)
{
    return cmd->msgchans;
}

int cmd_find_opts (const flux_cmd_t *cmd, const char **substrings)
{
    void *iter;
    int rv = 0;

    iter = zhash_first (cmd->opts);
    while (iter && !rv) {
        const char *key = zhash_cursor (cmd->opts);
        const char **str = substrings;
        while ((*str)) {
            if (strstr (key, (*str))) {
                rv++;
                break;
            }
            str++;
        }
        iter = zhash_next (cmd->opts);
    }

    return rv;
}


/*
 * vi: ts=4 sw=4 expandtab
 */
