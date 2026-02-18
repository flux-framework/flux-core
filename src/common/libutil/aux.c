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
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "ccan/str/str.h"

#include "aux.h"

struct aux_item {
    char *key;
    void *val;
    aux_free_f free_fn;
    struct aux_item *next;
};

/* Destroy an aux item.
 * It is assumed to already be unlinked from list.
 */
static void aux_item_destroy (struct aux_item *aux)
{
    if (aux) {
        int saved_errno = errno;
        if (aux->free_fn && aux->val)
            aux->free_fn (aux->val);
        free (aux);
        errno = saved_errno;
    }
}

/* Create an aux item.
 * The key (if any) is copied to the space following the item struct so the
 * item and the key can be co-located in memory, and allocated with one malloc.
 * Return item on success, NULL on failure with errno set (ENOMEM).
 */
static struct aux_item *aux_item_create (const char *key,
                                         void *val, aux_free_f free_fn)
{
    struct aux_item *aux;
    int keysize = key ? strlen (key) + 1 : 0;

    if (!(aux = calloc (1, sizeof (*aux) + keysize)))
        return NULL;
    if (key) {
        aux->key = (char *)(aux + 1);
        strcpy (aux->key, key);
    }
    aux->val = val;
    aux->free_fn = free_fn;
    return aux;
}

/* Delete from 'head' an aux item that was stored under 'key', if any.
 * Quit search once an item with a NULL key is found, since these come last.
 * 'head' is an in/out parameter.
 */
static void aux_item_delete (struct aux_item **head, const char *key)
{
    if (key && head) {
        struct aux_item *item;

        while ((item = *head) && item->key) {
            if (streq (item->key, key)) {
                *head = item->next;
                aux_item_destroy (item);
                break;
            }
            head = &item->next;
        }
    }
}

/* Find in 'head' an aux item stored under 'key'.
 * Quit search once an item with a NULL key is found, since these come last.
 * Returns item on success, NULL on failure.
 */
static struct aux_item *aux_item_find (struct aux_item *head, const char *key)
{
    if (key) {
        while (head && head->key) {
            if (streq (key, head->key))
                return head;
            head = head->next;
        }
    }
    return NULL;
}

/* Insert at the beginning of 'head' an aux 'item'.
 * 'head' is an in/out parameter.
 */
static void aux_item_insert (struct aux_item **head, struct aux_item *item)
{
    if (head && item) {
        if (*head)
            item->next = *head;
        *head = item;
    }
}

/* Insert item at the end of 'head'.
 * 'head' is an in/out parameter.
 */
static void aux_item_append (struct aux_item **head, struct aux_item *item)
{
    if (head && item) {
        while (*head)
            head = &(*head)->next;
        *head = item;
    }
}

/* Look up 'key' in 'head'.
 * Returns value on success, NULL on failure with errno set (EINVAL, ENOENT).
 */
void *aux_get (struct aux_item *head, const char *key)
{
    struct aux_item *item;

    if (!key) {
        errno = EINVAL;
        return NULL;
    }
    if (!(item = aux_item_find (head, key))) {
        errno = ENOENT;
        return NULL;
    }
    return item->val;
}

/* Insert ('key', 'value', 'free_fn') tuple in 'head'.
 * If 'key' is present in list, remove it first.
 * If 'key' is NULL, append item to the list rather than prepend,
 * so aux_get doesn't have to search keyless items.
 * 'head' is an in/out parameter.
 * Returns 0 on success, -1 on failure with errno set (EINVAL, ENOMEM).
 */
int aux_set (struct aux_item **head,
             const char *key, void *val, aux_free_f free_fn)
{
    struct aux_item *item;

    if (!head || (!key && !val) || (!val && free_fn) || (!key && !free_fn)) {
        errno = EINVAL;
        return -1;
    }
    aux_item_delete (head, key);
    if (val) {
        if (!(item = aux_item_create (key, val, free_fn)))
            return -1;
        if (key)
            aux_item_insert (head, item);
        else
            aux_item_append (head, item);
    }
    return 0;
}

void aux_delete_value (struct aux_item **head, const void *val)
{
    struct aux_item *item;

    if (head && val) {
        while ((item = *head)) {
            if (item->val == val) {
                *head = item->next;
                aux_item_destroy (item);
                break;
            }
            head = &item->next;
        }
    }
}

/* Destroy aux list 'head', calling destructors on items that have them.
 */
void aux_destroy (struct aux_item **head)
{
    if (head) {
        while (*head) {
            struct aux_item *item = *head;
            *head = item->next;
            aux_item_destroy (item);
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
