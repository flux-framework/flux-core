/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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

#include "ccan/str/str.h"

#include <jansson.h>

struct grudgeset {
    json_t *set;
    json_t *grudges;
};

void grudgeset_destroy (struct grudgeset *gset)
{
    if (gset) {
        int saved_errno = errno;
        json_decref (gset->set);
        json_decref (gset->grudges);
        free (gset);
        errno = saved_errno;
    }
}

static struct grudgeset *grudgeset_create (void)
{
    struct grudgeset *gset = calloc (1, sizeof (*gset));
    if (!gset
        || !(gset->set = json_array ())
        || !(gset->grudges = json_object ()))
        goto error;
    return gset;
error:
    grudgeset_destroy (gset);
    return NULL;
}

int grudgeset_used (struct grudgeset *gset, const char *val)
{
    if (!gset || !val)
        return 0;
    return (json_object_get (gset->grudges, val) != NULL);
}

static int json_array_find (json_t *array, const char *val)
{
    size_t index;
    json_t *entry;
    json_array_foreach (array, index, entry) {
        const char *s = json_string_value (entry);
        if (val && streq (val, s)) {
            return (int) index;
        }
    }
    return -1;
}

int grudgeset_contains (struct grudgeset *gset, const char *val)
{
    if (!gset
        || !val
        || json_array_find (gset->set, val) < 0)
        return 0;
    return 1;
}

int grudgeset_add (struct grudgeset **gsetp, const char *val)
{
    json_t *o = NULL;

    if (!gsetp || !val) {
        errno = EINVAL;
        return -1;
    }
    if (!*gsetp) {
        if (!(*gsetp = grudgeset_create ()))
            return -1;
    }
    if (grudgeset_used (*gsetp, val)) {
        errno = EEXIST;
        return -1;
    }
    if (!(o = json_string (val))
        || json_array_append_new ((*gsetp)->set, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    /* val is guaranteed not to exist in set->grudges */
    (void) json_object_set_new ((*gsetp)->grudges, val, json_null ());
    return 0;
}

int grudgeset_remove (struct grudgeset *gset, const char *val)
{
    int index;
    if (val == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (gset && (index = json_array_find (gset->set, val)) >= 0)
        return json_array_remove (gset->set, index);
    errno = ENOENT;
    return -1;
}

json_t *grudgeset_tojson (struct grudgeset *gset)
{
    if (gset)
        return gset->set;
    return NULL;
}

int grudgeset_size (struct grudgeset *gset)
{
    if (gset == NULL)
        return 0;
    return json_array_size (gset->set);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
