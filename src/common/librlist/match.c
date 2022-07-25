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

#include "src/common/libutil/errprintf.h"

#include "match.h"

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

static bool rnode_or (const struct rnode *n, json_t *args)
{
    json_t *constraint;
    size_t index;
    json_array_foreach (args, index, constraint) {
        if (rnode_match (n, constraint))
            return true;
    }
    /* No matches */
    return false;
}

static bool rnode_and (const struct rnode *n, json_t *args)
{
    json_t *constraint;
    size_t index;
    json_array_foreach (args, index, constraint) {
        if (!rnode_match (n, constraint))
            return false;
    }
    /* All matches */
    return true;
}

static bool rnode_not (const struct rnode *n, json_t *args)
{
    return !rnode_and (n, args);
}

bool rnode_match (const struct rnode *n, json_t *constraint)
{
    const char *op;
    json_t *args;

    if (!n || !constraint)
        return false;

    json_object_foreach (constraint, op, args) {
        bool result;
        if (strcmp (op, "properties") == 0)
            result = rnode_has_all (n, args);
        else if (strcmp (op, "or") == 0)
            result = rnode_or (n, args);
        else if (strcmp (op, "and") == 0)
            result = rnode_and (n, args);
        else if (strcmp (op, "not") == 0)
            result = rnode_not (n, args);
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

static int validate_conditional (const char *type,
                                 json_t *args,
                                 flux_error_t *errp)
{
    json_t *entry;
    size_t index;

    if (!json_is_array (args))
        return errprintf (errp, "%s operator value must be an array", type);

    json_array_foreach (args, index, entry) {
        if (rnode_match_validate (entry, errp) < 0)
            return -1;
    }
    return 0;
}

int rnode_match_validate (json_t *constraint, flux_error_t *errp)
{
    const char *op;
    json_t *args;

    if (!constraint || !json_is_object (constraint))
        return errprintf (errp, "constraint must be JSON object");

    json_object_foreach (constraint, op, args) {
        if (strcmp (op, "properties") == 0) {
            if (validate_properties (args, errp) < 0)
                return -1;
        }
        else if (strcmp (op, "or") == 0
                || strcmp (op, "and") == 0
                || strcmp (op, "not") == 0) {
            if (validate_conditional (op, args, errp) < 0)
                return -1;
        }
        else
            return errprintf (errp, "unknown constraint operator: %s", op);
    }
    return 0;
}

struct rnode *rnode_copy_match (const struct rnode *orig,
                                json_t *constraint)
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
