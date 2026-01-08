/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <flux/hostlist.h>
#include <flux/idset.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "match.h"

typedef bool (*match_f) (struct job_constraint *, const struct rnode *);

struct job_constraint {
    zlistx_t *values;
    match_f match;
};

static bool match_empty (struct job_constraint *c, const struct rnode *n)
{
    return true;
}

static struct job_constraint *job_constraint_new (flux_error_t *errp)
{
    struct job_constraint *c;
    if (!(c = calloc (1, sizeof (*c)))
        || !(c->values = zlistx_new ())) {
        job_constraint_destroy (c);
        errprintf (errp, "Out of memory");
        return NULL;
    }
    c->match = match_empty;
    return c;
}

static int add_idset_string (struct idset *idset, const char *s)
{
    int rc;
    struct idset *ids;

    if (!(ids = idset_decode (s)))
        return -1;
    rc = idset_add (idset, ids);
    idset_destroy (ids);
    return rc;
}

static struct idset *array_to_idset (json_t *idsets,
                                     flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct idset *idset = idset_create (0, IDSET_FLAG_AUTOGROW);

    if (!idset)
        return NULL;

    json_array_foreach (idsets, index, entry) {
        if (add_idset_string (idset, json_string_value (entry)) < 0) {
            char *s = json_dumps (idsets, 0);
            errprintf (errp,
                       "invalid idset '%s' in %s",
                       json_string_value (entry),
                       s);
            free (s);
            goto error;
        }
    }
    return idset;
error:
    idset_destroy (idset);
    return NULL;
}

static void destruct_idset (void **item)
{
    if (item) {
        idset_destroy (*item);
        *item = NULL;
    }
}

static bool match_idset (struct job_constraint *c,
                         const struct rnode *n)
{
    struct idset *idset = zlistx_first (c->values);
    return idset_test (idset, n->rank);
}

static struct job_constraint *create_idset_constraint (json_t *values,
                                                       flux_error_t *errp)
{
    struct job_constraint *c;
    struct idset *idset = array_to_idset (values, errp);
    if (!idset)
        return NULL;
    if (!(c = job_constraint_new (errp))
        || !zlistx_add_end (c->values, idset)) {
        job_constraint_destroy (c);
        idset_destroy (idset);
        return NULL;
    }
    zlistx_set_destructor (c->values, destruct_idset);
    c->match = match_idset;
    return c;
}

static struct hostlist *array_to_hostlist (json_t *hostlists,
                                           flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct hostlist *hl = hostlist_create ();

    if (!hl)
        return NULL;

    json_array_foreach (hostlists, index, entry) {
        if (hostlist_append (hl, json_string_value (entry)) < 0) {
            char *s = json_dumps (hostlists, 0);
            errprintf (errp,
                       "invalid hostlist '%s' in %s",
                       json_string_value (entry),
                       s);
            free (s);
            goto error;
        }
    }
    return hl;
error:
    hostlist_destroy (hl);
    return NULL;
}

static void destruct_hostlist (void **item)
{
    if (item) {
        hostlist_destroy (*item);
        *item = NULL;
    }
}

static bool match_hostlist (struct job_constraint *c,
                            const struct rnode *n)
{
    struct hostlist *hl = zlistx_first (c->values);
    if (!hl || hostlist_find (hl, n->hostname) < 0)
        return false;
    return true;
}

static struct job_constraint *create_hostlist_constraint (json_t *values,
                                                          flux_error_t *errp)
{
    struct job_constraint *c;
    struct hostlist *hl = array_to_hostlist (values, errp);
    if (!hl)
        return NULL;
    if (!(c = job_constraint_new (errp))
        || !zlistx_add_end (c->values, hl)) {
        job_constraint_destroy (c);
        hostlist_destroy (hl);
        return NULL;
    }
    zlistx_set_destructor (c->values, destruct_hostlist);
    c->match = match_hostlist;
    return c;
}

static bool rnode_has (const struct rnode *n, const char *property)
{
    const char *prop = property;
    bool match = false;
    bool negate = false;

    if (!property)
        return false;

    if (prop[0] == '^') {
        prop++;
        negate = true;
    }

    if ((n->properties && zhashx_lookup (n->properties, prop))
        || streq (n->hostname, prop))
        match = true;

    return negate ? !match : match;
}

static bool match_properties (struct job_constraint *c, const struct rnode *n)
{
    const char *property = zlistx_first (c->values);
    while (property) {
        if (!rnode_has (n, property))
            return false;
        property = zlistx_next (c->values);
    }
    return true;
}

bool job_constraint_match (struct job_constraint *c, const struct rnode *n)
{
    return c->match (c, n);
}

static const char *property_query_string_invalid (const char *s)
{
    /*  Return first invalid character.
     *  Invalid chaaracters are listed in RFC 20, but we specifically
     *   allow "^" since it is used as shorthand for `not`.
     */
    return strpbrk (s, "!&'\"`|()");
}

static void free_item (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static struct job_constraint *property_constraint (json_t *values,
                                                   flux_error_t *errp)
{
    struct job_constraint *c;
    json_t *entry;
    size_t index;

    if (!json_is_array (values)) {
        errprintf (errp, "properties value must be an array");
        return NULL;
    }
    if (!(c = job_constraint_new (errp))) {
        errprintf (errp, "Out of memory");
        return NULL;
    }
    zlistx_set_destructor (c->values, free_item);
    c->match = match_properties;

    json_array_foreach (values, index, entry) {
        const char *value;
        const char *invalid;
        char *s;

        if (!json_is_string (entry)) {
            errprintf (errp, "non-string property specified");
            goto err;
        }
        value = json_string_value (entry);
        if ((invalid = property_query_string_invalid (value))) {
            errprintf (errp,
                       "invalid character '%c' in property \"%s\"",
                       *invalid,
                       value);
            goto err;
        }
        if (!(s = strdup (value))) {
            errprintf (errp, "strdup (\"%s\"): out of memory", value);
            goto err;
        }
        if (!zlistx_add_end (c->values, s)) {
            errprintf (errp, "zlistx_add_end: out of memory");
            free (s);
            goto err;
        }
    }
    return c;
err:
    job_constraint_destroy (c);
    return NULL;
}

static void job_constraint_destructor (void **item)
{
    if (item) {
        job_constraint_destroy (*item);
        *item = NULL;
    }
}

static bool match_and (struct job_constraint *c, const struct rnode *n)
{
    struct job_constraint *cp = zlistx_first (c->values);
    while (cp) {
        if (!cp->match (cp, n))
            return false;
        cp = zlistx_next (c->values);
    }
    return true;
}

static bool match_or (struct job_constraint *c, const struct rnode *n)
{
    struct job_constraint *cp = zlistx_first (c->values);
    if (!cp)
        return true;
    while (cp) {
        if (cp->match (cp, n))
            return true;
        cp = zlistx_next (c->values);
    }
    return false;
}

static bool match_not (struct job_constraint *c, const struct rnode *n)
{
    return !match_and (c, n);
}

static struct job_constraint *conditional_constraint (const char *type,
                                                      json_t *values,
                                                      flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct job_constraint *c;

    if (!json_is_array (values)) {
        errprintf (errp, "%s operator value must be an array", type);
        return NULL;
    }

    if (!(c = job_constraint_new (errp)))
        return NULL;
    if (streq (type, "and"))
        c->match = match_and;
    else if (streq (type, "or"))
        c->match = match_or;
    else if (streq (type, "not"))
        c->match = match_not;
    zlistx_set_destructor (c->values, job_constraint_destructor);

    json_array_foreach (values, index, entry) {
        struct job_constraint *cp = job_constraint_create (entry, errp);
        if (!cp || !(zlistx_add_end (c->values, cp))) {
            errprintf (errp, "Out of memory");
            job_constraint_destroy (c);
            job_constraint_destroy (cp);
            return NULL;
        }
    }
    return c;
}

void job_constraint_destroy (struct job_constraint *c)
{
    if (c) {
        int saved_errno = errno;
        zlistx_destroy (&c->values);
        free (c);
        errno = saved_errno;
    }
}

struct job_constraint *job_constraint_create (json_t *constraint,
                                              flux_error_t *errp)
{
    const char *op;
    json_t *values;

    if (!constraint || !json_is_object (constraint)) {
        errprintf (errp, "constraint must be JSON object");
        return NULL;
    }
    if (json_object_size (constraint) > 1) {
        errprintf (errp, "constraint must only contain 1 element");
        return NULL;
    }
    json_object_foreach (constraint, op, values) {
        if (streq (op, "properties"))
            return property_constraint (values, errp);
        else if (streq (op, "hostlist"))
            return create_hostlist_constraint (values, errp);
        else if (streq (op, "ranks"))
            return create_idset_constraint (values, errp);
        else if (streq (op, "or") || streq (op, "and") || streq (op, "not"))
            return conditional_constraint (op, values, errp);
        else {
            errprintf (errp, "unknown constraint operator: %s", op);
            return NULL;
        }
    }
    return job_constraint_new (errp);
}

bool rnode_match (const struct rnode *n,
                  struct job_constraint *constraint)
{
    if (!n || !constraint)
        return false;
    return constraint->match (constraint, n);
}

struct rnode *rnode_copy_match (const struct rnode *orig,
                                struct job_constraint *constraint)
{
    struct rnode *n = NULL;
    if (rnode_match (orig, constraint)) {
        if ((n = rnode_copy (orig)))
            n->up = orig->up;
    }
    return n;
}

/* vi: ts=4 sw=4 expandtab
 */
