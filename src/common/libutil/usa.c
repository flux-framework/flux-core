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

#include "usa.h"

#include "xzmalloc.h"

#include <stdio.h>

unique_string_array_t *usa_alloc ()
{
    return xzmalloc (sizeof(unique_string_array_t));
}

void usa_init (unique_string_array_t *item)
{
    item->clean = false;
    item->sep = sdsempty ();
    item->string_cache = sdsempty ();
    vec_init (&item->components);
}

unique_string_array_t *usa_new ()
{
    unique_string_array_t *item = usa_alloc ();
    usa_init (item);
    return item;
}

void usa_clear (unique_string_array_t *item)
{
    int i;
    sds to_free;
    if (!item)
        return;

    sdsclear (item->sep);
    sdsclear (item->string_cache);
    vec_foreach (&item->components, to_free, i)
    {
        sdsfree (to_free);
    }
    vec_clear (&item->components);
    item->clean = false;
}

void usa_destroy (unique_string_array_t *item)
{
    usa_clear (item);
    vec_deinit (&item->components);
    sdsfree (item->sep);
    sdsfree (item->string_cache);
    free (item);
}

void usa_set (unique_string_array_t *item, const char *value)
{
    item->clean = false;
    usa_clear (item);
    vec_push (&item->components, sdsnew (value));
}

void usa_push (unique_string_array_t *item, const char *str)
{
    vec_insert (&item->components, 0, sdsnew (str));
}

void usa_push_back (unique_string_array_t *item, const char *str)
{
    vec_push (&item->components, sdsnew (str));
}

#if 0
static void usa_print_all (unique_string_array_t *item)
{
    int i;
    sds part;
    vec_foreach (&item->components, part, i)
    {
        printf ("%d: %s\n", i, part);
    }
}
#endif

int usa_remove (unique_string_array_t *item, const char *str)
{
    int idx = usa_find_idx (item, str);
    if (idx >= 0) {
        vec_splice (&item->components, idx, 1);
    }
    return idx;
}

void usa_split_and_set (unique_string_array_t *item, const char *value)
{
    item->clean = false;
    usa_clear (item);
    usa_split_and_push (item, value, false);
}

void usa_split_and_push (unique_string_array_t *item,
                         const char *value,
                         bool before)
{
    int value_count = 0, i;
    bool insert_on_blank = true;

    if (!value || strlen (value) == 0)
        return;

    sds part;
    sds *new_parts = sdssplitlen (
        value, strlen (value), item->sep, strlen (item->sep), &value_count);

    for (i = 0, part = new_parts[i]; i < value_count;
         i++, part = new_parts[i]) {
        if (!sdslen (part) && insert_on_blank) {
            // This odd dance is for lua's default path item
            new_parts[i] = sdscpy (new_parts[i], item->sep);
            insert_on_blank = false;
        }

        if (before) {
            usa_remove (item, part);
            usa_push (item, part);
        } else {
            int idx = usa_find_idx (item, part);
            if (idx < 0)
                usa_push_back (item, part);
            else
                continue;  // No update, not dirty
        }
        item->clean = false;
    }

    sdsfreesplitres (new_parts, value_count);
}

void usa_set_separator (unique_string_array_t *item, const char *separator)
{
    if (item)
        item->sep = sdscpy (item->sep, separator);
}

const char *usa_get_joined (unique_string_array_t *item)
{
    if (!item)
        return NULL;

    if (usa_length (item) == 1)
        return usa_data (item)[0];

    if (!item->clean) {
        sdsfree (item->string_cache);
        item->string_cache = sdsjoinsds (item->components.data,
                                         item->components.length,
                                         item->sep,
                                         sdslen (item->sep));
    }

    return item->string_cache;
}

const char *usa_find (unique_string_array_t *item, const char *str)
{
    int idx = usa_find_idx (item, str);
    return idx >= 0 ? item->components.data[idx] : NULL;
}

int usa_find_idx (unique_string_array_t *item, const char *str)
{
    int i;
    sds part;
    vec_foreach (&item->components, part, i)
    {
        if (!strcmp (part, str)) {
            return i;
        }
    }
    return -1;
}

void usa_deduplicate (unique_string_array_t *item)
{
    int i;
    sds part;
    vec_str_t v = item->components;
    vec_init (&item->components);
    vec_foreach (&v, part, i)
    {
        if (!usa_find (item, part)) {
            vec_push (&item->components, part);
        } else {
            sdsfree (part);
        }
    }
    vec_deinit (&v);
}
