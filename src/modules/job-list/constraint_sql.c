/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include "ccan/str/str.h"

#include "constraint_sql.h"
#include "match_util.h"

static int create_userid_query (json_t *values, char **query, flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    char *q = NULL;
    json_array_foreach (values, index, entry) {
        uint32_t userid;
        if (!json_is_integer (entry)) {
            errprintf (errp, "userid value must be an integer");
            goto error;
        }
        userid = json_integer_value (entry);
        /* special case FLUX_USERID_UNKNOWN matches all, so no query result */
        if (userid == FLUX_USERID_UNKNOWN)
            continue;
        if (!q) {
            if (asprintf (&q, "userid = %u", userid) < 0) {
                errno = ENOMEM;
                goto error;
            }
        }
        else {
            char *tmp;
            if (asprintf (&tmp, "%s OR userid = %u", q, userid) < 0) {
                errno = ENOMEM;
                goto error;
            }
            free (q);
            q = tmp;
        }
    }
    (*query) = q;
    return 0;
error:
    free (q);
    return -1;
}

static int create_string_query (json_t *values,
                                const char *op,
                                char **query,
                                flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    char *q = NULL;
    json_array_foreach (values, index, entry) {
        const char *str;
        if (!json_is_string (entry)) {
            errprintf (errp, "%s value must be a string", op);
            goto error;
        }
        str = json_string_value (entry);
        if (!q) {
            if (asprintf (&q, "%s = '%s'", op, str) < 0) {
                errno = ENOMEM;
                goto error;
            }
        }
        else {
            char *tmp;
            if (asprintf (&tmp, "%s OR %s = '%s'", q, op, str) < 0) {
                errno = ENOMEM;
                goto error;
            }
            free (q);
            q = tmp;
        }
    }
    (*query) = q;
    return 0;
error:
    free (q);
    return -1;
}

static int create_name_query (json_t *values,
                              char **query,
                              flux_error_t *errp)
{
    return create_string_query (values, "name", query, errp);
}

static int create_queue_query (json_t *values,
                              char **query,
                              flux_error_t *errp)
{
    return create_string_query (values, "queue", query, errp);
}

static int create_bitmask_query (const char *col,
                                 json_t *values,
                                 array_to_bitmask_f array_to_bitmask_cb,
                                 char **query,
                                 flux_error_t *errp)
{
    char *q = NULL;
    int tmp;
    if ((tmp = array_to_bitmask_cb (values, errp)) < 0)
        return -1;
    if (asprintf (&q, "(%s & %d) > 0", col, tmp) < 0) {
        errno = ENOMEM;
        return -1;
    }
    (*query) = q;
    return 0;
}

static int create_states_query (json_t *values,
                                char **query,
                                flux_error_t *errp)
{
    return create_bitmask_query ("state",
                                 values,
                                 array_to_states_bitmask,
                                 query,
                                 errp);
}

static int create_results_query (json_t *values,
                                 char **query,
                                 flux_error_t *errp)
{
    return create_bitmask_query ("result",
                                 values,
                                 array_to_results_bitmask,
                                 query,
                                 errp);
}

static int create_timestamp_query (const char *type,
                                   json_t *values,
                                   char **query,
                                   flux_error_t *errp)
{
    const char *str;
    const char *comp;
    char *q = NULL;
    double t;
    char *endptr;
    json_t *v = json_array_get (values, 0);

    if (!v) {
        errprintf (errp, "timestamp value not specified");
        return -1;
    }
    if (!json_is_string (v)) {
        errprintf (errp, "%s value must be a string", type);
        return -1;
    }
    str = json_string_value (v);
    if (strstarts (str, ">=")) {
        comp = ">=";
        str += 2;
    }
    else if (strstarts (str, "<=")) {
        comp = "<=";
        str += 2;
    }
    else if (strstarts (str, ">")) {
        comp = ">";
        str +=1;
    }
    else if (strstarts (str, "<")) {
        comp = "<";
        str += 1;
    }
    else {
        errprintf (errp, "timestamp comparison operator not specified");
        return -1;
    }

    errno = 0;
    t = strtod (str, &endptr);
    if (errno != 0 || *endptr != '\0') {
        errprintf (errp, "Invalid timestamp value specified");
        return -1;
    }
    if (t < 0.0) {
        errprintf (errp, "timestamp value must be >= 0.0");
        return -1;
    }

    if (asprintf (&q, "%s %s %s", type, comp, str) < 0) {
        errno = ENOMEM;
        return -1;
    }

    (*query) = q;
    return 0;
}

static int conditional_query (const char *type,
                              json_t *values,
                              char **query,
                              flux_error_t *errp)
{
    char *q = NULL;
    char *cond;
    json_t *entry;
    size_t index;

    if (streq (type, "and"))
        cond = "AND";
    else if (streq (type, "or"))
        cond = "OR";
    else /* streq (type, "not") */
        /* we will "NOT" it at the end */
        cond = "AND";

    json_array_foreach (values, index, entry) {
        char *subquery;
        if (constraint2sql (entry, &subquery, errp) < 0)
            goto error;
        if (!q)
            q = subquery;
        else if (subquery) {
            char *tmp;
            if (asprintf (&tmp, "%s %s %s", q, cond, subquery) < 0) {
                free (subquery);
                errno = ENOMEM;
                goto error;
            }
            free (q);
            q = tmp;
        }
    }
    if (q && streq (type, "not")) {
        char *tmp;
        if (asprintf (&tmp, "NOT (%s)", q) < 0) {
            errno = ENOMEM;
            goto error;
        }
        free (q);
        q = tmp;
    }
    (*query) = q;
    return 0;

error:
    free (q);
    return -1;
}

int constraint2sql (json_t *constraint, char **query, flux_error_t *errp)
{
    char *q = NULL;
    char *rv = NULL;
    const char *op;
    json_t *values;

    if (!query)
        return -1;

    if (!constraint)
        return 0;

    if (!json_is_object (constraint)) {
        errprintf (errp, "constraint must be JSON object");
        return -1;
    }
    if (json_object_size (constraint) > 1) {
        errprintf (errp, "constraint must only contain 1 element");
        return -1;
    }
    json_object_foreach (constraint, op, values) {
        int ret;
        if (!json_is_array (values)) {
            errprintf (errp, "operator %s values not an array", op);
            goto error;
        }
        if (streq (op, "userid"))
            ret = create_userid_query (values, &q, errp);
        else if (streq (op, "name"))
            ret = create_name_query (values, &q, errp);
        else if (streq (op, "queue"))
            ret = create_queue_query (values, &q, errp);
        else if (streq (op, "states"))
            ret = create_states_query (values, &q, errp);
        else if (streq (op, "results"))
            ret = create_results_query (values, &q, errp);
        else if (streq (op, "hostlist")
                 || streq (op, "ranks")) {
            /* no hostlist or ranks column matching, no conversion to
             * be done
             */
            ret = 0;
        }
        else if (streq (op, "t_submit")
                 || streq (op, "t_depend")
                 || streq (op, "t_run")
                 || streq (op, "t_cleanup")
                 || streq (op, "t_inactive"))
            ret = create_timestamp_query (op, values, &q, errp);
        else if (streq (op, "or") || streq (op, "and") || streq (op, "not"))
            ret = conditional_query (op, values, &q, errp);
        else {
            errprintf (errp, "unknown constraint operator: %s", op);
            goto error;
        }
        if (ret < 0)
            goto error;
    }
    if (q) {
        if (asprintf (&rv, "(%s)", q) < 0) {
            errno = ENOMEM;
            goto error;
        }
        free (q);
    }
    (*query) = rv;
    return 0;

error:
    free (q);
    return -1;
}

/* vi: ts=4 sw=4 expandtab
 */
