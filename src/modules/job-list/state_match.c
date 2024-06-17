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

#include "state_match.h"
#include "match_util.h"

/* MATCH_ALWAYS - constraint always matches job in state X
 * MATCH_MAYBE - constraint maybe matches job in state X
 * MATCH_NEVER - constraint never matches job in state X
 *
 * examples:
 *
 * states=depend
 *
 * This constraint ALWAYS matches a job in state depend and NEVER matches
 * a job in any other job state.
 *
 * userid=42
 *
 * This constraint MAYBE matches a job in job state X because
 * the job state does not matter, it depends on the userid.
 *
 * NOT (userid=42)
 *
 * This constraint MAYBE matches a job in job state X because again,
 * it depends on the userid.  The NOT of a MAYBE is still MAYBE.
 *
 * (states=depend OR userid=42)
 *
 * This constraint ALWAYS matches a job in state depend, but MAYBE matches
 * a job in any other job state, since it depends on the userid.
 *
 * (states=depend AND userid=42)
 *
 * This constraint MAYBE matches a job state in state depend, because
 * it depends on the userid.  It NEVER matches a job in any other
 * state.
 *
 */
typedef enum {
    MATCH_NOTSET,
    MATCH_ALWAYS,
    MATCH_MAYBE,
    MATCH_NEVER,
} state_match_t;

typedef state_match_t (*match_f) (struct state_constraint *,
                                  flux_job_state_t state);

struct state_constraint {
    zlistx_t *values;
    match_f match;
};

static state_match_t match_always (struct state_constraint *c, flux_job_state_t state)
{
    return MATCH_ALWAYS;
}

static state_match_t match_maybe (struct state_constraint *c, flux_job_state_t state)
{
    return MATCH_MAYBE;
}

static struct state_constraint *state_constraint_new (match_f match_cb,
                                                      destructor_f destructor_cb,
                                                      flux_error_t *errp)
{
    struct state_constraint *c;
    if (!(c = calloc (1, sizeof (*c)))
        || !(c->values = zlistx_new ())) {
        state_constraint_destroy (c);
        errprintf (errp, "Out of memory");
        return NULL;
    }
    c->match = match_cb;
    if (destructor_cb)
        zlistx_set_destructor (c->values, destructor_cb);
    return c;
}

static void state_constraint_destructor (void **item)
{
    if (item) {
        state_constraint_destroy (*item);
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

static state_match_t match_states (struct state_constraint *c,
                                   flux_job_state_t state)
{
    int *states = zlistx_first (c->values);
    if ((*states) & state)
        return MATCH_ALWAYS;
    return MATCH_NEVER;
}

static struct state_constraint *create_states_constraint (json_t *values,
                                                          flux_error_t *errp)
{
    struct state_constraint *c;
    int *bitmask = NULL;
    int tmp;
    if ((tmp = array_to_states_bitmask (values, errp)) < 0)
        return NULL;
    /* if no states specified, returns MATCH_ALWAYS */
    if (!tmp)
        return state_constraint_new (match_always, NULL, errp);
    if (!(bitmask = malloc (sizeof (*bitmask))))
        return NULL;
    (*bitmask) = tmp;
    if (!(c = state_constraint_new (match_states, wrap_free, errp))
        || !zlistx_add_end (c->values, bitmask)) {
        state_constraint_destroy (c);
        free (bitmask);
        return NULL;
    }
    return c;
}

static state_match_t match_result (struct state_constraint *c, flux_job_state_t state)
{
    if (state != FLUX_JOB_STATE_INACTIVE)
        return MATCH_NEVER;
    return MATCH_MAYBE;
}

/* N.B. Not all job states can be reached, e.g. a pending job is
 * canceled, so it never reaches the RUN state.  That is still handled
 * here in this logic.  e.g. a constraint on `t_run` can MAYBE pass if
 * the job state is INACTIVE.  We don't know if `t_run` was ever set,
 * but since it can MAYBE be set, we must check.
 */
static state_match_t match_t_submit (struct state_constraint *c,
                                     flux_job_state_t state)
{
    return MATCH_MAYBE;
}

static state_match_t match_t_depend (struct state_constraint *c,
                                     flux_job_state_t state)
{
    if (state >= FLUX_JOB_STATE_DEPEND)
        return MATCH_MAYBE;
    return MATCH_NEVER;
}

static state_match_t match_t_run (struct state_constraint *c,
                                  flux_job_state_t state)
{
    if (state >= FLUX_JOB_STATE_RUN)
        return MATCH_MAYBE;
    return MATCH_NEVER;
}

static state_match_t match_t_cleanup (struct state_constraint *c,
                                      flux_job_state_t state)
{
    if (state >= FLUX_JOB_STATE_CLEANUP)
        return MATCH_MAYBE;
    return MATCH_NEVER;
}

static state_match_t match_t_inactive (struct state_constraint *c,
                                       flux_job_state_t state)
{
    if (state == FLUX_JOB_STATE_INACTIVE)
        return MATCH_MAYBE;
    return MATCH_NEVER;
}

static struct state_constraint *create_timestamp_constraint (const char *type,
                                                             flux_error_t *errp)
{
    struct state_constraint *c;
    match_f cb;

    if (streq (type, "t_submit"))
        cb = match_t_submit;
    else if (streq (type, "t_depend"))
        cb = match_t_depend;
    else if (streq (type, "t_run"))
        cb = match_t_run;
    else if (streq (type, "t_cleanup"))
        cb = match_t_cleanup;
    else /* streq (type, "t_inactive") */
        cb = match_t_inactive;

    if (!(c = state_constraint_new (cb, NULL, errp)))
        return NULL;
    return c;
}

static state_match_t match_and (struct state_constraint *c,
                                flux_job_state_t state)
{
    state_match_t rv = MATCH_NOTSET;
    struct state_constraint *cp = zlistx_first (c->values);
    while (cp) {
        /* This is an and statement, so if it a match is NEVER, we
         * know that this constraint will return NEVER all the time.
         *
         * An ALWAYS can be demoted to a MAYBE and a MAYBE can be
         * demoted to NEVER, so we keep iterating the match callbacks.
         */
        state_match_t m = cp->match (cp, state);
        if (m == MATCH_NEVER)
            return MATCH_NEVER;
        else if (rv == MATCH_NOTSET)
            rv = m;
        else if (rv == MATCH_ALWAYS
                 && m == MATCH_MAYBE) {
            rv = MATCH_MAYBE;
        }
        /* else if rv == MATCH_MAYBE,
         *  m == MATCH_MAYBE or m == MAYBE_ALWAYS,
         *  rv stays MATCH_MAYBE
         */
        cp = zlistx_next (c->values);
    }
    /* empty op return MATCH_ALWAYS */
    if (rv == MATCH_NOTSET)
        return MATCH_ALWAYS;
    return rv;
}

static state_match_t match_or (struct state_constraint *c,
                               flux_job_state_t state)
{
    state_match_t rv = MATCH_NOTSET;
    struct state_constraint *cp = zlistx_first (c->values);
    while (cp) {
        /* This is an or statement, so if it a match is ALWAYS, we
         * know that this constraint will return ALWAYS all the time.
         *
         * A NEVER can be promoted to to a MAYBE and a MAYBE can be
         * promoted to an ALWAYS, so we keep on iterating the match
         * callbacks.
         */
        state_match_t m = cp->match (cp, state);
        if (m == MATCH_ALWAYS)
            return MATCH_ALWAYS;
        else if (rv == MATCH_NOTSET)
            rv = m;
        else if (rv == MATCH_NEVER
                 && m == MATCH_MAYBE)
            rv = MATCH_MAYBE;
        /* else if rv == MATCH_MAYBE,
         *  m == MATCH_NEVER or m == MAYBE_MAYBE,
         *  rv stays MATCH_MAYBE
         */
        cp = zlistx_next (c->values);
    }
    /* empty op return MATCH_ALWAYS */
    if (rv == MATCH_NOTSET)
        return MATCH_ALWAYS;
    return rv;
}

static state_match_t match_not (struct state_constraint *c,
                                flux_job_state_t state)
{
    state_match_t m = match_and (c, state);
    if (m == MATCH_ALWAYS)
        return MATCH_NEVER;
    else if (m == MATCH_NEVER)
        return MATCH_ALWAYS;
    return MATCH_MAYBE;
}

static struct state_constraint *conditional_constraint (const char *type,
                                                        json_t *values,
                                                        flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    struct state_constraint *c;
    match_f match_cb;

    if (streq (type, "and"))
        match_cb = match_and;
    else if (streq (type, "or"))
        match_cb = match_or;
    else /* streq (type, "not") */
        match_cb = match_not;

    if (!(c = state_constraint_new (match_cb,
                                    state_constraint_destructor,
                                    errp)))
        return NULL;

    json_array_foreach (values, index, entry) {
        struct state_constraint *cp = state_constraint_create (entry, errp);
        if (!cp)
            goto error;
        if (!zlistx_add_end (c->values, cp)) {
            errprintf (errp, "Out of memory");
            state_constraint_destroy (cp);
            goto error;
        }
    }
    return c;

 error:
    state_constraint_destroy (c);
    return NULL;
}

void state_constraint_destroy (struct state_constraint *constraint)
{
    if (constraint) {
        int saved_errno = errno;
        zlistx_destroy (&constraint->values);
        free (constraint);
        errno = saved_errno;
    }
}

struct state_constraint *state_constraint_create (json_t *constraint, flux_error_t *errp)
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
            if (streq (op, "userid")
                || streq (op, "name")
                || streq (op, "queue")
                || streq (op, "hostlist")
                || streq (op, "ranks"))
                return state_constraint_new (match_maybe, NULL, errp);
            else if (streq (op, "results"))
                return state_constraint_new (match_result, NULL, errp);
            else if (streq (op, "states"))
                return create_states_constraint (values, errp);
            else if (streq (op, "t_submit")
                     || streq (op, "t_depend")
                     || streq (op, "t_run")
                     || streq (op, "t_cleanup")
                     || streq (op, "t_inactive"))
                return create_timestamp_constraint (op, errp);
            else if (streq (op, "or") || streq (op, "and") || streq (op, "not"))
                return conditional_constraint (op, values, errp);
            else {
                errprintf (errp, "unknown constraint operator: %s", op);
                return NULL;
            }
        }
    }
    return state_constraint_new (match_always, NULL, errp);
}

bool state_match (int state, struct state_constraint *constraint)
{
    int valid_states = (FLUX_JOB_STATE_ACTIVE | FLUX_JOB_STATE_INACTIVE);

    if (!state
        || (state & ~valid_states)
        || ((state & (state - 1)) != 0 /* classic is more than 1 bit set trick */
            && state != FLUX_JOB_STATE_PENDING
            && state != FLUX_JOB_STATE_RUNNING
            && state != FLUX_JOB_STATE_ACTIVE)
        || !constraint)
        return false;

    if ((state & (state - 1)) != 0) {
        if (state == FLUX_JOB_STATE_PENDING)
            return (state_match (FLUX_JOB_STATE_DEPEND, constraint)
                    || state_match (FLUX_JOB_STATE_PRIORITY, constraint)
                    || state_match (FLUX_JOB_STATE_SCHED, constraint));
        else if (state == FLUX_JOB_STATE_RUNNING)
            return (state_match (FLUX_JOB_STATE_RUN, constraint)
                    || state_match (FLUX_JOB_STATE_CLEANUP, constraint));
        else /* state == FLUX_JOB_STATE_ACTIVE */
            return (state_match (FLUX_JOB_STATE_PENDING, constraint)
                    || state_match (FLUX_JOB_STATE_RUNNING, constraint));
    }
    else {
        state_match_t m;
        m = constraint->match (constraint, state);
        if (m == MATCH_ALWAYS || m == MATCH_MAYBE)
            return true;
        return false;
    }
}

/* vi: ts=4 sw=4 expandtab
 */
