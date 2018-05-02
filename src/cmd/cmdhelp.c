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
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/sds.h"

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

static json_t *command_list_file_read (const char *path)
{
    json_t *o;
    json_error_t error;

    if (!(o = json_load_file (path, 0, &error))) {
        log_msg ("%s::%d: %s", path, error.line, error.text);
        return NULL;
    }
    if (!json_is_array (o)) {
        log_msg ("%s: not a JSON array", path);
        json_decref (o);
        return (NULL);
    }

    return (o);
}

static int command_list_read (zhash_t *h, const char *path)
{
    int i;
    int rc = -1;
    int n = 0;
    json_t *o = NULL;

    if (!(o = command_list_file_read (path)))
        goto out;

    n = json_array_size (o);
    for (i = 0; i < n; i++) {
        const char *category;
        const char *command;
        const char *description;
        json_t *entry;
        zlist_t *zl;

        if (!(entry = json_array_get (o, i))) {
            log_msg ("%s: entry %d is not an object", path, i);
            goto out;
        }
        if (json_unpack (entry, "{s:s s:s s:s}",
                                "category", &category,
                                "command", &command,
                                "description", &description) < 0) {
            log_msg ("%s: Missing element in JSON entry %d", path, i);
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
            free (s);
        }
        zlist_append (zl, cmdhelp_create (command, description));
    }
    rc = 0;

out:
    json_decref (o);
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
            log_err_exit ("%s: failed to read content\n", file);
    }
    globfree (&gl);

out:
    return (zh);
}

static void emit_command_help_from_pattern (const char *pattern, FILE *fp)
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

void emit_command_help (const char *plist, FILE *fp)
{
    int i, count;
    sds *p;
    if (!(p = sdssplitlen (plist, strlen (plist), ":", 1, &count)))
        return;
    for (i = 0; i < count; i++)
        emit_command_help_from_pattern (p[i], fp);
    sdsfreesplitres (p, count);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
