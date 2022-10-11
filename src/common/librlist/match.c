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

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/aux.h"

#include "match.h"

struct job_constraint {
    json_t *constraint;
    struct aux_item *aux;
};

static struct hostlist * array_to_hostlist (struct job_constraint *jc,
                                            const char *s,
                                            json_t *hostlists,
                                            flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct hostlist *hl = hostlist_create ();

    if (!hl)
        return NULL;

    json_array_foreach (hostlists, index, entry) {
        if (hostlist_append (hl, json_string_value (entry)) < 0) {
            errprintf (errp,
                       "invalid hostlist '%s' in %s",
                       json_string_value (entry),
                       s);
            goto error;
        }
    }
    if (job_constraint_aux_set (jc,
                                s,
                                hl,
                                (flux_free_f) hostlist_destroy) < 0) {
        errprintf (errp, "out of memory");
        goto error;
    }

    return hl;
error:
    hostlist_destroy (hl);
    return NULL;
}

static int validate_hostlist (struct job_constraint *jc,
                              json_t *args,
                              flux_error_t *errp)
{
    char *s;
    struct hostlist *hl;

    if (!json_is_array (args))
        return errprintf (errp, "hostlist operator argument not an array");

    s = json_dumps (args, JSON_COMPACT);
    hl = array_to_hostlist (jc, s, args, errp);
    free (s);
    if (!hl)
        return -1;
    return 0;
}

static bool rnode_in_hostlist (const struct rnode *n,
                               struct job_constraint *jc,
                               json_t *hostlists)
{
    struct hostlist *hl;
    char *s = json_dumps (hostlists, JSON_COMPACT);

    if (!(hl = job_constraint_aux_get (jc, s)))
        hl = array_to_hostlist (jc, s, hostlists, NULL);
    free (s);
    if (!hl || hostlist_find (hl, n->hostname) < 0)
        return false;

    return true;
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

static struct idset * array_to_idset (struct job_constraint *jc,
                                      const char *s,
                                      json_t *ids,
                                      flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct idset *idset = idset_create (0, IDSET_FLAG_AUTOGROW);

    if (!idset)
        return NULL;

    json_array_foreach (ids, index, entry) {
        if (add_idset_string (idset, json_string_value (entry)) < 0) {
            errprintf (errp,
                       "invalid ranks '%s' in %s",
                       json_string_value (entry),
                       s);
            goto error;
        }
    }
    if (job_constraint_aux_set (jc,
                                s,
                                idset,
                                (flux_free_f) idset_destroy) < 0) {
        errprintf (errp, "out of memory");
        goto error;
    }

    return idset;
error:
    idset_destroy (idset);
    return NULL;
}

static int validate_idset (struct job_constraint *jc,
                           json_t *args,
                           flux_error_t *errp)
{
    char *s;
    struct idset *idset;

    if (!json_is_array (args))
        return errprintf (errp, "idset operator argument not an array");

    s = json_dumps (args, JSON_COMPACT);
    idset = array_to_idset (jc, s, args, errp);
    free (s);
    if (!idset)
        return -1;
    return 0;
}

static bool rnode_in_idset (const struct rnode *n,
                            struct job_constraint *jc,
                            json_t *ids)
{
    struct idset *idset;
    char *s = json_dumps (ids, JSON_COMPACT);

    if (!(idset = job_constraint_aux_get (jc, s)))
        idset = array_to_idset (jc, s, ids, NULL);
    free (s);
    if (idset && idset_test (idset, n->rank))
        return true;
    return false;
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
        || strcmp (n->hostname, prop) == 0)
        match = true;

    return negate ? !match : match;
}

static bool rnode_has_all (const struct rnode *n, json_t *properties)
{
    json_t *entry;
    size_t index;

    json_array_foreach (properties, index, entry) {
        if (!rnode_has (n, json_string_value (entry)))
            return false;
    }
    return true;
}

static bool rnode_or (const struct rnode *n,
                      struct job_constraint *jc,
                      json_t *args)
{
    json_t *constraint;
    size_t index;
    json_array_foreach (args, index, constraint) {
        if (rnode_match (n, jc, constraint))
            return true;
    }
    /* No matches */
    return false;
}

static bool rnode_and (const struct rnode *n,
                       struct job_constraint *jc,
                       json_t *args)
{
    json_t *constraint;
    size_t index;
    json_array_foreach (args, index, constraint) {
        if (!rnode_match (n, jc, constraint))
            return false;
    }
    /* All matches */
    return true;
}

static bool rnode_not (const struct rnode *n,
                       struct job_constraint *jc,
                       json_t *args)
{
    return !rnode_and (n, jc, args);
}

bool rnode_match (const struct rnode *n,
                  struct job_constraint *jc,
                  json_t *constraint)
{
    const char *op;
    json_t *args;

    if (!n || !constraint)
        return false;

    json_object_foreach (constraint, op, args) {
        bool result;
        if (strcmp (op, "properties") == 0)
            result = rnode_has_all (n, args);
        else if (strcmp (op, "hostlist") == 0)
            result = rnode_in_hostlist (n, jc, args);
        else if (strcmp (op, "ranks") == 0)
            result = rnode_in_idset (n, jc, args);
        else if (strcmp (op, "or") == 0)
            result = rnode_or (n, jc, args);
        else if (strcmp (op, "and") == 0)
            result = rnode_and (n, jc, args);
        else if (strcmp (op, "not") == 0)
            result = rnode_not (n, jc, args);
        else
            result = false;

        /*  Multiple keys in dict are treated like AND */
        if (result == false)
            return false;
    }
    return true;
}

static char * property_query_string_invalid (const char *s)
{
    /*  Return first invalid character.
     *  Invalid chaaracters are listed in RFC 20, but we specifically
     *   allow "^" since it is used as shorthand for `not`.
     */
    return strpbrk (s, "!&'\"`|()");
}

static int validate_properties (json_t *args, flux_error_t *errp)
{
    json_t *entry;
    size_t index;

    if (!json_is_array (args))
        return errprintf (errp, "properties value must be an array");

    json_array_foreach (args, index, entry) {
        const char *value;
        const char *invalid;

        if (!json_is_string (entry))
            return errprintf (errp, "non-string property specified");
        value = json_string_value (entry);
        if ((invalid = property_query_string_invalid (value)))
            return errprintf (errp,
                              "invalid character '%c' in property \"%s\"",
                              *invalid,
                              value);
    }
    return 0;
}

static int validate_conditional (struct job_constraint *jc,
                                 const char *type,
                                 json_t *args,
                                 flux_error_t *errp)
{
    json_t *entry;
    size_t index;

    if (!json_is_array (args))
        return errprintf (errp, "%s operator value must be an array", type);

    json_array_foreach (args, index, entry) {
        if (rnode_match_validate (jc, entry, errp) < 0)
            return -1;
    }
    return 0;
}

int rnode_match_validate (struct job_constraint *jc,
                          json_t *constraint,
                          flux_error_t *errp)
{
    const char *op;
    json_t *args;

    if (!jc || !json_is_object (constraint))
        return errprintf (errp, "constraint must be JSON object");

    json_object_foreach (constraint, op, args) {
        if (strcmp (op, "properties") == 0) {
            if (validate_properties (args, errp) < 0)
                return -1;
        }
        else if (strcmp (op, "hostlist") == 0) {
            if (validate_hostlist (jc, args, errp) < 0)
                return -1;
        }
        else if (strcmp (op, "ranks") == 0) {
            if (validate_idset (jc, args, errp) < 0)
                return -1;
        }
        else if (strcmp (op, "or") == 0
                || strcmp (op, "and") == 0
                || strcmp (op, "not") == 0) {
            if (validate_conditional (jc, op, args, errp) < 0)
                return -1;
        }
        else
            return errprintf (errp, "unknown constraint operator: %s", op);
    }
    return 0;
}

struct rnode *rnode_copy_match (const struct rnode *orig,
                                struct job_constraint *jc)
{
    struct rnode *n = NULL;
    if (!jc) {
        errno = EINVAL;
        return NULL;
    }
    if (rnode_match (orig, jc, jc->constraint)) {
        if ((n = rnode_copy (orig)))
            n->up = orig->up;
    }
    return n;
}

void job_constraint_destroy (struct job_constraint *jc)
{
    if (jc) {
        int saved_errno = errno;
        aux_destroy (&jc->aux);
        json_decref (jc->constraint);
        free (jc);
        errno = saved_errno;
    }
}

struct job_constraint *job_constraint_create (json_t *constraint,
                                              flux_error_t *errp)
{
    struct job_constraint *jc;

    if (!(jc = calloc (1, sizeof (*jc))))
        return NULL;
    jc->constraint = json_incref (constraint);

    if (rnode_match_validate (jc, constraint, errp) < 0) {
        job_constraint_destroy (jc);
        return NULL;
    }

   return (jc);
}

int job_constraint_aux_set (struct job_constraint *jc,
                            const char *key,
                            void *val,
                            flux_free_f free_fn)
{
    if (!jc) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&jc->aux, key, val, free_fn);
}

void * job_constraint_aux_get (struct job_constraint *jc, const char *key)
{
    if (!jc) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (jc->aux, key);
}


/* vi: ts=4 sw=4 expandtab
 */
