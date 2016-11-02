/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

/* lru_cache.c - simple lru cache in c */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <czmq.h>

#include "lru_cache.h"

struct lru_entry {
    lru_cache_t *lru;
    char *key;
    void *item;

    struct lru_entry *prev;
    struct lru_entry *next;
};

struct lru_cache {
    int maxsize;
    int count;

    lru_cache_free_f freefn;

    zhash_t *entries;
    struct lru_entry *first, *last;
};


static struct lru_entry *
lru_entry_create (lru_cache_t *lru, const char *key, void *item)
{
    struct lru_entry *l = malloc (sizeof (*l));
    if (!l || !(l->key = strdup (key))) {
        free (l);
        return (NULL);
    }
    l->lru = lru;
    l->item = item;
    l->prev = l->next = NULL;
    return (l);
}


static void lru_entry_destroy (struct lru_entry *l)
{
    if (l) {
        if (l->lru && l->lru->freefn)
            (*l->lru->freefn) (l->item);
        free (l->key);
        free (l);
    }
}

static void lru_entry_remove (lru_cache_t *lru, struct lru_entry *l)
{
    /* Unlink l from queue first */
    if (l->prev == NULL)
        lru->first = l->next;
    else
        l->prev->next = l->next;
    if (l->next == NULL)
        lru->last = l->prev;
    else
        l->next->prev = l->prev;
}

static void lru_entry_push (lru_cache_t *lru, struct lru_entry *l)
{
    /* add to front of the LRU list */
    l->next = lru->first;
    if (lru->last == NULL) /* empty list */
        lru->last = lru->first = l;
    else { /* Update head of list */
        lru->first->prev = l;
        lru->first = l;
    }
}

static void lru_entry_purge (lru_cache_t *lru, struct lru_entry *l)
{
    lru_entry_remove (lru, l);

    /* Now remove entry from hash, this will result in memory
     *  for `l` being freed.
     */
    zhash_delete (lru->entries, l->key);
    lru->count--;
}

static void lru_purge_last (lru_cache_t *lru)
{
    if (lru->last == NULL)
        return;
    lru_entry_purge (lru, lru->last);
}

static void *
lru_entry_enqueue (lru_cache_t *lru, const char *key, void *value)
{
    struct lru_entry *l = lru_entry_create (lru, key, value);
    if (!l)
        return (NULL);

    if (lru->count == lru->maxsize)
        lru_purge_last (lru);
    lru_entry_push (lru, l);
    lru->count++;

    /* Place entry on hash, and add cleanup function */
    if (zhash_insert (lru->entries, key, l) < 0)
        abort ();
    zhash_freefn (lru->entries, key, (zhash_free_fn *) lru_entry_destroy);

    return (l->item);
}

static void *
lru_entry_requeue (lru_cache_t *lru, struct lru_entry *l)
{
    lru_entry_remove (lru, l);
    lru_entry_push (lru, l);
    return (l->item);
}

/*
 *  Public functions:
 */

void lru_cache_destroy (lru_cache_t *lru)
{
    zhash_destroy (&lru->entries);
    free (lru);
}

lru_cache_t * lru_cache_create (int maxsize)
{
    lru_cache_t *lru = NULL;
    zhash_t *zh = NULL;

    if (!(zh = zhash_new ()) || !(lru = malloc (sizeof (*lru)))) {
        free (lru);
        free (zh);
        return (NULL);
    }

    lru->maxsize = maxsize;
    lru->count = 0;
    lru->entries = zh;
    lru->first = lru->last = NULL;
    lru->freefn = NULL;

    return (lru);
}

void lru_cache_set_free_f (lru_cache_t *lru, lru_cache_free_f fn)
{
    lru->freefn = fn;
}

void *lru_cache_get (lru_cache_t *lru, const char *key)
{
    struct lru_entry *l;
    if ((l = zhash_lookup (lru->entries, key)))
        return lru_entry_requeue (lru, l);
    return (NULL);
}

bool lru_cache_check (lru_cache_t *lru, const char *key)
{
    if (zhash_lookup (lru->entries, key) == NULL)
        return false;
    return true;
}

int lru_cache_put (lru_cache_t *lru, const char *key, void *value)
{
    if (lru_cache_get (lru, key)) {
        errno = EEXIST;
        return (-1);
    }
    if (lru_entry_enqueue (lru, key, value) < 0)
        return (-1);
    return (0);
}

int lru_cache_remove (lru_cache_t *lru, const char *key)
{
    struct lru_entry *l;
    if (!(l = zhash_lookup (lru->entries, key)))
        return (-1);
    lru_entry_purge (lru, l);
    return (0);
}

int lru_cache_size (lru_cache_t *lru)
{
    return (lru->count);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
