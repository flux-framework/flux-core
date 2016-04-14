/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
# include "config.h"
#endif
#include <glob.h>
#include <string.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/sds.h"
#include "src/common/libutil/shortjson.h"

#include "cmdhelp.h"

struct cmdhelp {
    sds cmd;
    sds description;
};

static struct cmdhelp *cmdhelp_create (const char *cmd, const char *desc)
{
    struct cmdhelp *ch = xzmalloc (sizeof (*ch));
    ch->cmd = sdsnew (cmd);
    ch->description = sdsnew (desc);
    if (!ch->cmd || !ch->description) {
        free (ch);
        return (NULL);
    }
    return (ch);
}

static void cmdhelp_destroy (struct cmdhelp **p)
{
    if (*p) {
        struct cmdhelp *ch = *p;
        sdsfree (ch->cmd);
        sdsfree (ch->description);
        free (ch);
        *p = NULL;
    }
}

static void cmd_list_destroy (zlist_t *zl)
{
    struct cmdhelp *ch;
    ch = zlist_first (zl);
    while (ch) {
        cmdhelp_destroy (&ch);
        ch = zlist_next (zl);
    }
    zlist_destroy (&zl);
}

static json_object *command_list_file_read (const char *path)
{
    FILE *fp = NULL;
    json_object *o = NULL;
    enum json_tokener_error e = json_tokener_success;
    json_tokener *tok = json_tokener_new ();

    if (tok == NULL) {
        msg ("json_tokener_new: Out of memory");
        return (NULL);
    }

    if (!(fp = fopen (path, "r"))) {
        err ("%s", path);
        return (NULL);
    }

    do {
        int len;
        char buf [4096];
        char *s;
        if (!(s = fgets (buf, sizeof (buf), fp)))
            break;
        len = strlen (s);
        o = json_tokener_parse_ex (tok, s, len);
    } while ((e = json_tokener_get_error(tok)) == json_tokener_continue);

    fclose (fp);
    json_tokener_free (tok);

    if (!o || e != json_tokener_success) {
        msg ("%s: %s", path, o ? json_tokener_error_desc (e) : "premature EOF");
        return (NULL);
    }

    if (json_object_get_type (o) != json_type_array) {
        msg ("%s: not a JSON array", path);
        json_object_put (o);
        return (NULL);
    }

    return (o);
}

static int command_list_read (zhash_t *h, const char *path)
{
    int i;
    int rc = -1;
    int n = 0;
    json_object *o = NULL;
    struct array_list *l;

    o = command_list_file_read (path);

    if (!(l = json_object_get_array (o))) {
        msg ("%s: failed to get array list from JSON", path);
        goto out;
    }

    n = array_list_length (l);
    for (i = 0; i < n; i++) {
        json_object *entry = array_list_get_idx (l, i);
        const char *category;
        const char *command;
        const char *description;
        zlist_t *zl;
        if (!Jget_str (entry, "category", &category)
            || !Jget_str (entry, "command", &command)
            || !Jget_str (entry, "description", &description)) {
            msg ("%s: Missing element in JSON entry %d", path, i);
            goto out;
        }
        if (!(zl = zhash_lookup (h, category))) {
            char *s = strdup (category);
            if (s == NULL)
                goto out;
            zl = zlist_new ();
            //zlist_set_destructor (zl, (czmq_destructor *) cmdhelp_destroy);
            zhash_insert (h, s, (void *) zl);
            zhash_freefn (h, s, (zhash_free_fn *) cmd_list_destroy);
        }
        zlist_append (zl, cmdhelp_create (command, description));
    }
    rc = 0;

out:
    if (o)
        json_object_put (o);
    return (rc);

}

static void emit_command_list_category (zhash_t *zh, const char *cat, FILE *fp)
{
    struct cmdhelp *c;
    zlist_t *zl = zhash_lookup (zh, cat);

    fprintf (fp, "Common commands from flux-%s:\n", cat);
    c = zlist_first (zl);
    while (c) {
        fprintf (fp, "   %-18s %s\n", c->cmd, c->description);
        c = zlist_next (zl);
    }
}

/* strcmp() ensuring "core" is always sorted to front:
 */
static int categorycmp (const char *s1, const char *s2)
{
    if (strcmp (s1, "core") == 0)
        return (-1);
    if (strcmp (s2, "core") == 0)
        return (1);
    return strcmp (s1, s2);
}

#if CZMQ_VERSION < CZMQ_MAKE_VERSION(3,0,1)
static bool category_cmp (const char *s1, const char *s2)
{
    return (categorycmp (s1, s2) > 0);
}
#else
static int category_cmp (const char *s1, const char *s2)
{
    return categorycmp (s1, s2);
}
#endif

zhash_t *get_command_list_hash (const char *pattern)
{
    int rc;
    size_t i;
    glob_t gl;
    zhash_t *zh = NULL;

    rc = glob (pattern, GLOB_ERR, NULL, &gl);
    switch (rc) {
        case 0:
            break; /* have results, fall-through. */
        case GLOB_ABORTED:
            /* No help.d directory? */
            goto out;
            break;
        case GLOB_NOMATCH:
            goto out;
            break;
        default:
            fprintf (stderr, "glob: unknown error %d\n", rc);
            break;
    }

    zh = zhash_new ();
    //zhash_set_destructor (zh, (czmq_destructor *) zlist_destroy);
    for (i = 0; i < gl.gl_pathc; i++) {
        const char *file = gl.gl_pathv[i];
        if (command_list_read (zh, file) < 0)
            err_exit ("%s: failed to read content\n", file);
    }
    globfree (&gl);

out:
    return (zh);
}

void emit_command_help (const char *pattern, FILE *fp)
{
    zhash_t *zh = NULL;
    zlist_t *keys = NULL;
    const char *cat;

    zh = get_command_list_hash (pattern);
    if (zh == NULL)
        return;

    keys = zhash_keys (zh);
    zlist_sort (keys, (zlist_compare_fn *) category_cmp);

    cat = zlist_first (keys);
    while (cat) {
        emit_command_list_category (zh, cat, fp);
        if ((cat = zlist_next (keys)))
            fprintf (fp, "\n");
    }
    zlist_destroy (&keys);
    zhash_destroy (&zh);
    return;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
