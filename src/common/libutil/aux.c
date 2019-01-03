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
        free (aux->key);
        free (aux);
        errno = saved_errno;
    }
}

/* Create an aux item.
 * Return item on success, NULL on failure with errno set (ENOMEM).
 */
static struct aux_item *aux_item_create (const char *key,
                                         void *val, aux_free_f free_fn)
{
    struct aux_item *aux;

    if (!(aux = calloc (1, sizeof (*aux))))
        return NULL;
    if (key && !(aux->key = strdup (key)))
        goto error;
    aux->val = val;
    aux->free_fn = free_fn;
    return aux;
error:
    aux_item_destroy (aux);
    return NULL;
}

/* Delete from 'head' an aux item that was stored under 'key', if any.
 * 'head' is an in/out parameter.
 */
static void aux_item_delete (struct aux_item **head, const char *key)
{
    if (key && head) {
        struct aux_item *item;

        while ((item = *head)) {
            if (item->key && !strcmp (item->key, key)) {
                *head = item->next;
                aux_item_destroy (item);
                break;
            }
            head = &item->next;
        }
    }
}

/* Find in 'head' an aux item stored under 'key'.
 * Returns item on success, NULL on failure.
 */
static struct aux_item *aux_item_find (struct aux_item *head, const char *key)
{
    if (key) {
        while (head) {
            if (head->key && !strcmp (key, head->key))
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
        aux_item_insert (head, item);
    }
    return 0;
}

/* Destroy aux list 'head', calling destructors on items that have them.
 */
void aux_destroy (struct aux_item **head)
{
    if (head) {
        while (*head) {
            struct aux_item *next = (*head)->next;
            aux_item_destroy (*head);
            *head = next;
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
