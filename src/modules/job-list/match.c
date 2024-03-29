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

#include "match.h"
#include "match_util.h"

typedef bool (*match_f) (struct list_constraint *, const struct job *);

struct list_constraint {
    zlistx_t *values;
    match_f match;
};

typedef enum {
    MATCH_T_SUBMIT = 1,
    MATCH_T_DEPEND = 2,
    MATCH_T_RUN = 3,
    MATCH_T_CLEANUP = 4,
    MATCH_T_INACTIVE = 5,
} match_timestamp_type_t;

typedef enum {
    MATCH_GREATER_THAN_EQUAL = 1,
    MATCH_LESS_THAN_EQUAL = 2,
    MATCH_GREATER_THAN = 3,
    MATCH_LESS_THAN = 4,
} match_comparison_t;

struct timestamp_value {
    double t_value;
    match_timestamp_type_t t_type;
    match_comparison_t t_comp;
};

static void timestamp_value_destroy (void *data)
{
    if (data) {
        int save_errno = errno;
        free (data);
        errno = save_errno;
    }
}

/* zlistx_set_destructor */
static void wrap_timestamp_value_destroy (void **item)
{
    if (item) {
        struct timestamp_value *tv = *item;
        timestamp_value_destroy (tv);
        (*item) = NULL;
    }
}

static struct timestamp_value *timestamp_value_create (
    double t_value,
    const char *type,
    match_comparison_t comp)
{
    struct timestamp_value *tv;

    if (!(tv = calloc (1, sizeof (*tv))))
        return NULL;
    tv->t_value = t_value;

    if (streq (type, "t_submit"))
        tv->t_type = MATCH_T_SUBMIT;
    else if (streq (type, "t_depend"))
        tv->t_type = MATCH_T_DEPEND;
    else if (streq (type, "t_run"))
        tv->t_type = MATCH_T_RUN;
    else if (streq (type, "t_cleanup"))
        tv->t_type = MATCH_T_CLEANUP;
    else if (streq (type, "t_inactive"))
        tv->t_type = MATCH_T_INACTIVE;
    else
        goto cleanup;

    tv->t_comp = comp;
    return tv;

cleanup:
    timestamp_value_destroy (tv);
    return NULL;
}

static struct timestamp_value *timestamp_value_create_str (
    const char *t_value,
    const char *type,
    match_comparison_t comp,
    flux_error_t *errp)
{
    double t;
    char *endptr;
    errno = 0;
    t = strtod (t_value, &endptr);
    if (errno != 0 || *endptr != '\0') {
        errprintf (errp, "Invalid timestamp value specified");
        return NULL;
    }
    if (t < 0.0) {
        errprintf (errp, "timestamp value must be >= 0.0");
        return NULL;
    }
    return timestamp_value_create (t, type, comp);
}

static bool match_true (struct list_constraint *c, const struct job *job)
{
    return true;
}

static struct list_constraint *list_constraint_new (match_f match_cb,
                                                    destructor_f destructor_cb,
                                                    flux_error_t *errp)
{
    struct list_constraint *c;
    if (!(c = calloc (1, sizeof (*c)))
        || !(c->values = zlistx_new ())) {
        list_constraint_destroy (c);
        errprintf (errp, "Out of memory");
        return NULL;
    }
    c->match = match_cb;
    if (destructor_cb)
        zlistx_set_destructor (c->values, destructor_cb);
    return c;
}

static void list_constraint_destructor (void **item)
{
    if (item) {
        list_constraint_destroy (*item);
        *item = NULL;
    }
}

/* zlistx_set_destructor */
static void wrap_free (void **item)
{
    if (item) {
        free (*item);
        (*item) = NULL;
    }
}

static bool match_userid (struct list_constraint *c,
                          const struct job *job)
{
    uint32_t *userid = zlistx_first (c->values);
    while (userid) {
        if ((*userid) == FLUX_USERID_UNKNOWN)
            return true;
        if ((*userid) == job->userid)
            return true;
        userid = zlistx_next (c->values);
    }
    return false;
}

static struct list_constraint *create_userid_constraint (json_t *values,
                                                         flux_error_t *errp)
{
    struct list_constraint *c;
    json_t *entry;
    size_t index;

    if (!(c = list_constraint_new (match_userid, wrap_free, errp)))
        return NULL;
    json_array_foreach (values, index, entry) {
        uint32_t *userid;
        if (!json_is_integer (entry)) {
            errprintf (errp, "userid value must be an integer");
            goto error;
        }
        if (!(userid = malloc (sizeof (*userid))))
            goto error;
        (*userid) = json_integer_value (entry);
        if (!zlistx_add_end (c->values, userid)) {
            free (userid);
            goto error;
        }
    }
    return c;
 error:
    list_constraint_destroy (c);
    return NULL;
}

static struct list_constraint *create_string_constraint (const char *op,
                                                         json_t *values,
                                                         match_f match_cb,
                                                         flux_error_t *errp)
{
    struct list_constraint *c;
    json_t *entry;
    size_t index;

    if (!(c = list_constraint_new (match_cb, wrap_free, errp)))
        return NULL;
    json_array_foreach (values, index, entry) {
        char *s = NULL;
        if (!json_is_string (entry)) {
            errprintf (errp, "%s value must be a string", op);
            goto error;
        }
        if (!(s = strdup (json_string_value (entry))))
            return NULL;
        if (!zlistx_add_end (c->values, s)) {
            free (s);
            goto error;
        }
    }
    return c;
 error:
    list_constraint_destroy (c);
    return NULL;
}

static bool match_name (struct list_constraint *c, const struct job *job)
{
    const char *name = zlistx_first (c->values);
    while (name) {
        if (job->name && streq (name, job->name))
            return true;
        name = zlistx_next (c->values);
    }
    return false;
}

static struct list_constraint *create_name_constraint (json_t *values,
                                                       flux_error_t *errp)
{
    return create_string_constraint ("name", values, match_name, errp);
}

static bool match_queue (struct list_constraint *c, const struct job *job)
{
    const char *queue = zlistx_first (c->values);
    while (queue) {
        if (job->queue && streq (queue, job->queue))
            return true;
        queue = zlistx_next (c->values);
    }
    return false;
}

static struct list_constraint *create_queue_constraint (json_t *values,
                                                        flux_error_t *errp)
{
    return create_string_constraint ("queue", values, match_queue, errp);
}

static struct list_constraint *create_bitmask_constraint (
    const char *op,
    json_t *values,
    match_f match_cb,
    array_to_bitmask_f array_to_bitmask_cb,
    flux_error_t *errp)
{
    struct list_constraint *c;
    int *bitmask = NULL;
    int tmp;
    if ((tmp = array_to_bitmask_cb (values, errp)) < 0)
        return NULL;
    if (!(bitmask = malloc (sizeof (*bitmask))))
        return NULL;
    (*bitmask) = tmp;
    if (!(c = list_constraint_new (match_cb, wrap_free, errp))
        || !zlistx_add_end (c->values, bitmask)) {
        list_constraint_destroy (c);
        free (bitmask);
        return NULL;
    }
    return c;
}

static bool match_states (struct list_constraint *c, const struct job *job)
{
    int *states = zlistx_first (c->values);
    return ((*states) & job->state);
}

static struct list_constraint *create_states_constraint (json_t *values,
                                                         flux_error_t *errp)
{
    return create_bitmask_constraint ("states",
                                      values,
                                      match_states,
                                      array_to_states_bitmask,
                                      errp);
}

static bool match_results (struct list_constraint *c,
                           const struct job *job)
{
    int *results = zlistx_first (c->values);
    if (job->state != FLUX_JOB_STATE_INACTIVE)
        return false;
    return ((*results) & job->result);
}

static int array_to_results_bitmask (json_t *values, flux_error_t *errp)
{
    int results = 0;
    json_t *entry;
    size_t index;
    int valid_results = (FLUX_JOB_RESULT_COMPLETED
                         | FLUX_JOB_RESULT_FAILED
                         | FLUX_JOB_RESULT_CANCELED
                         | FLUX_JOB_RESULT_TIMEOUT);

    json_array_foreach (values, index, entry) {
        flux_job_result_t result;
        if (json_is_string (entry)) {
            const char *resultstr = json_string_value (entry);
            if (flux_job_strtoresult (resultstr, &result) < 0) {
                errprintf (errp,
                           "invalid results value '%s' specified",
                           resultstr);
                return -1;
            }
        }
        else if (json_is_integer (entry)) {
            result = json_integer_value (entry);
            if (result & ~valid_results) {
                errprintf (errp,
                           "invalid results value '%Xh' specified",
                           result);
                return -1;
            }
        }
        else {
            errprintf (errp, "results value invalid type");
            return -1;
        }
        results |= result;
    }
    return results;
}

static struct list_constraint *create_results_constraint (json_t *values,
                                                          flux_error_t *errp)
{
    return create_bitmask_constraint ("results",
                                      values,
                                      match_results,
                                      array_to_results_bitmask,
                                      errp);
}

static bool match_timestamp (struct list_constraint *c,
                             const struct job *job)
{
    struct timestamp_value *tv = zlistx_first (c->values);
    double t;

    if (tv->t_type == MATCH_T_SUBMIT)
        t = job->t_submit;
    else if (tv->t_type == MATCH_T_DEPEND) {
        /* if submit_version < 1, it means it was not set.  This is
         * before the introduction of event `validate` after 0.41.1.
         * Before the introduction of this event, t_submit and
         * t_depend are the same.
         */
        if (job->submit_version < 1)
            t = job->t_submit;
        else if (job->states_mask & FLUX_JOB_STATE_DEPEND)
            t = job->t_depend;
        else
            return false;
    }
    else if (tv->t_type == MATCH_T_RUN
             && (job->states_mask & FLUX_JOB_STATE_RUN))
        t = job->t_run;
    else if (tv->t_type == MATCH_T_CLEANUP
             && (job->states_mask & FLUX_JOB_STATE_CLEANUP))
        t = job->t_cleanup;
    else if (tv->t_type == MATCH_T_INACTIVE
             && (job->states_mask & FLUX_JOB_STATE_INACTIVE))
        t = job->t_inactive;
    else
        return false;

    if (tv->t_comp == MATCH_GREATER_THAN_EQUAL)
        return t >= tv->t_value;
    else if (tv->t_comp == MATCH_LESS_THAN_EQUAL)
        return t <= tv->t_value;
    else if (tv->t_comp == MATCH_GREATER_THAN)
        return t > tv->t_value;
    else /* tv->t_comp == MATCH_LESS_THAN */
        return t < tv->t_value;
}

static struct list_constraint *create_timestamp_constraint (const char *type,
                                                            json_t *values,
                                                            flux_error_t *errp)
{
    struct timestamp_value *tv = NULL;
    struct list_constraint *c;
    const char *str;
    json_t *v = json_array_get (values, 0);

    if (!v) {
        errprintf (errp, "timestamp value not specified");
        return NULL;
    }
    if (!json_is_string (v)) {
        errprintf (errp, "%s value must be a string", type);
        return NULL;
    }
    str = json_string_value (v);
    if (strstarts (str, ">="))
        tv = timestamp_value_create_str (str + 2,
                                         type,
                                         MATCH_GREATER_THAN_EQUAL,
                                         errp);
    else if (strstarts (str, "<="))
        tv = timestamp_value_create_str (str + 2,
                                         type,
                                         MATCH_LESS_THAN_EQUAL,
                                         errp);
    else if (strstarts (str, ">"))
        tv = timestamp_value_create_str (str + 1,
                                         type,
                                         MATCH_GREATER_THAN,
                                         errp);
    else if (strstarts (str, "<"))
        tv = timestamp_value_create_str (str + 1,
                                         type,
                                         MATCH_LESS_THAN,
                                         errp);
    else
        errprintf (errp, "timestamp comparison operator not specified");

    if (!tv)
        return NULL;

    if (!(c = list_constraint_new (match_timestamp,
                                   wrap_timestamp_value_destroy,
                                   errp))
        || !zlistx_add_end (c->values, tv)) {
        list_constraint_destroy (c);
        timestamp_value_destroy (tv);
        return NULL;
    }
    return c;
}

static bool match_and (struct list_constraint *c, const struct job *job)
{
    struct list_constraint *cp = zlistx_first (c->values);
    while (cp) {
        if (!cp->match (cp, job))
            return false;
        cp = zlistx_next (c->values);
    }
    return true;
}

static bool match_or (struct list_constraint *c, const struct job *job)
{
    struct list_constraint *cp = zlistx_first (c->values);
    /* no values in "or" defined as true per RFC31 */
    if (!cp)
        return true;
    while (cp) {
        if (cp->match (cp, job))
            return true;
        cp = zlistx_next (c->values);
    }
    return false;
}

static bool match_not (struct list_constraint *c, const struct job *job)
{
    return !match_and (c, job);
}

static struct list_constraint *conditional_constraint (const char *type,
                                                       json_t *values,
                                                       flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct list_constraint *c;
    match_f match_cb;

    if (streq (type, "and"))
        match_cb = match_and;
    else if (streq (type, "or"))
        match_cb = match_or;
    else /* streq (type, "not") */
        match_cb = match_not;

    if (!(c = list_constraint_new (match_cb, list_constraint_destructor, errp)))
        return NULL;

    json_array_foreach (values, index, entry) {
        struct list_constraint *cp = list_constraint_create (entry, errp);
        if (!cp)
            goto error;
        if (!zlistx_add_end (c->values, cp)) {
            errprintf (errp, "Out of memory");
            list_constraint_destroy (cp);
            goto error;
        }
    }
    return c;

 error:
    list_constraint_destroy (c);
    return NULL;
}

void list_constraint_destroy (struct list_constraint *constraint)
{
    if (constraint) {
        int saved_errno = errno;
        zlistx_destroy (&constraint->values);
        free (constraint);
        errno = saved_errno;
    }
}

struct list_constraint *list_constraint_create (json_t *constraint, flux_error_t *errp)
{
    const char *op;
    json_t *values;

    if (constraint) {
        if (!json_is_object (constraint)) {
            errprintf (errp, "constraint must be JSON object");
            return NULL;
        }
        if (json_object_size (constraint) > 1) {
            errprintf (errp, "constraint must only contain 1 element");
            return NULL;
        }
        json_object_foreach (constraint, op, values) {
            if (!json_is_array (values)) {
                errprintf (errp, "operator %s values not an array", op);
                return NULL;
            }
            if (streq (op, "userid"))
                return create_userid_constraint (values, errp);
            else if (streq (op, "name"))
                return create_name_constraint (values, errp);
            else if (streq (op, "queue"))
                return create_queue_constraint (values, errp);
            else if (streq (op, "states"))
                return create_states_constraint (values, errp);
            else if (streq (op, "results"))
                return create_results_constraint (values, errp);
            else if (streq (op, "t_submit")
                     || streq (op, "t_depend")
                     || streq (op, "t_run")
                     || streq (op, "t_cleanup")
                     || streq (op, "t_inactive"))
                return create_timestamp_constraint (op, values, errp);
            else if (streq (op, "or") || streq (op, "and") || streq (op, "not"))
                return conditional_constraint (op, values, errp);
            else {
                errprintf (errp, "unknown constraint operator: %s", op);
                return NULL;
            }
        }
    }
    return list_constraint_new (match_true, NULL, errp);
}

bool job_match (const struct job *job, struct list_constraint *constraint)
{
    if (!job || !constraint)
        return false;
    return constraint->match (constraint, job);
}

/* vi: ts=4 sw=4 expandtab
 */
