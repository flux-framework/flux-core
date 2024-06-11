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
#include "job_util.h"

typedef int (*match_f) (struct list_constraint *,
                        const struct job *,
                        unsigned int *,
                        flux_error_t *);

struct list_constraint {
    struct match_ctx *mctx;
    zlistx_t *values;
    match_f match;
    unsigned int comparisons;   /* total across multiple calls to job_match() */
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

#define MIN_MATCH_HOSTLIST 1024

struct timestamp_value {
    double t_value;
    match_timestamp_type_t t_type;
    match_comparison_t t_comp;
};

#define CONSTRAINT_COMPARISON_MAX 1000000

static inline int inc_check_comparison (struct match_ctx *mctx,
                                        unsigned int *comparisons,
                                        flux_error_t *errp)
{
    if (mctx->max_comparisons
        && (++(*comparisons)) > mctx->max_comparisons) {
        errprintf (errp,
                   "Excessive comparisons made, "
                   "limit search via states or since");
        return -1;
    }
    return 0;
}

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

static int match_true (struct list_constraint *c,
                       const struct job *job,
                       unsigned int *comparisons,
                       flux_error_t *errp)
{
    if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
        return -1;
    return 1;
}

static struct list_constraint *list_constraint_new (struct match_ctx *mctx,
                                                    match_f match_cb,
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
    c->mctx = mctx;
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

static int match_userid (struct list_constraint *c,
                         const struct job *job,
                         unsigned int *comparisons,
                         flux_error_t *errp)
{
    uint32_t *userid = zlistx_first (c->values);
    while (userid) {
        if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
            return -1;
        if ((*userid) == FLUX_USERID_UNKNOWN)
            return 1;
        if ((*userid) == job->userid)
            return 1;
        userid = zlistx_next (c->values);
    }
    return 0;
}

static struct list_constraint *create_userid_constraint (struct match_ctx *mctx,
                                                         json_t *values,
                                                         flux_error_t *errp)
{
    struct list_constraint *c;
    json_t *entry;
    size_t index;

    if (!(c = list_constraint_new (mctx, match_userid, wrap_free, errp)))
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

static struct list_constraint *create_string_constraint (struct match_ctx *mctx,
                                                         const char *op,
                                                         json_t *values,
                                                         match_f match_cb,
                                                         flux_error_t *errp)
{
    struct list_constraint *c;
    json_t *entry;
    size_t index;

    if (!(c = list_constraint_new (mctx, match_cb, wrap_free, errp)))
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

static int match_name (struct list_constraint *c,
                       const struct job *job,
                       unsigned int *comparisons,
                       flux_error_t *errp)
{
    const char *name = zlistx_first (c->values);
    while (name) {
        if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
            return -1;
        if (job->name && streq (name, job->name))
            return 1;
        name = zlistx_next (c->values);
    }
    return 0;
}

static struct list_constraint *create_name_constraint (struct match_ctx *mctx,
                                                       json_t *values,
                                                       flux_error_t *errp)
{
    return create_string_constraint (mctx, "name", values, match_name, errp);
}

static int match_queue (struct list_constraint *c,
                        const struct job *job,
                        unsigned int *comparisons,
                        flux_error_t *errp)
{
    const char *queue = zlistx_first (c->values);
    while (queue) {
        if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
            return -1;
        if (job->queue && streq (queue, job->queue))
            return 1;
        queue = zlistx_next (c->values);
    }
    return 0;
}

static struct list_constraint *create_queue_constraint (struct match_ctx *mctx,
                                                        json_t *values,
                                                        flux_error_t *errp)
{
    return create_string_constraint (mctx, "queue", values, match_queue, errp);
}

static struct list_constraint *create_bitmask_constraint (
    struct match_ctx *mctx,
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
    if (!(c = list_constraint_new (mctx, match_cb, wrap_free, errp))
        || !zlistx_add_end (c->values, bitmask)) {
        list_constraint_destroy (c);
        free (bitmask);
        return NULL;
    }
    return c;
}

static int match_states (struct list_constraint *c,
                         const struct job *job,
                         unsigned int *comparisons,
                         flux_error_t *errp)
{
    int *states = zlistx_first (c->values);
    if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
        return -1;
    return ((*states) & job->state) ? 1 : 0;
}

static struct list_constraint *create_states_constraint (struct match_ctx *mctx,
                                                         json_t *values,
                                                         flux_error_t *errp)
{
    return create_bitmask_constraint (mctx,
                                      "states",
                                      values,
                                      match_states,
                                      array_to_states_bitmask,
                                      errp);
}

static int match_results (struct list_constraint *c,
                          const struct job *job,
                          unsigned int *comparisons,
                          flux_error_t *errp)
{
    int *results = zlistx_first (c->values);
    if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
        return -1;
    if (job->state != FLUX_JOB_STATE_INACTIVE)
        return 0;
    return ((*results) & job->result) ? 1 : 0;
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

static struct list_constraint *create_results_constraint (struct match_ctx *mctx,
                                                          json_t *values,
                                                          flux_error_t *errp)
{
    return create_bitmask_constraint (mctx,
                                      "results",
                                      values,
                                      match_results,
                                      array_to_results_bitmask,
                                      errp);
}

static int match_hostlist (struct list_constraint *c,
                           const struct job *job,
                           unsigned int *comparisons,
                           flux_error_t *errp)
{
    struct hostlist *hl = zlistx_first (c->values);
    const char *host;

    /* nodelist may not exist if job never ran */
    if (!job->nodelist)
        return 0;
    if (!job->nodelist_hl) {
        /* hack to remove const */
        struct job *jobtmp = (struct job *)job;
        if (!(jobtmp->nodelist_hl = hostlist_decode (job->nodelist)))
            return 0;
    }
    host = hostlist_first (hl);
    while (host) {
        if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
            return -1;
        if (hostlist_find (job->nodelist_hl, host) >= 0)
            return 1;
        host = hostlist_next (hl);
    }
    return 0;
}

/* zlistx_set_destructor */
static void wrap_hostlist_destroy (void **item)
{
    if (item) {
        struct hostlist *hl = *item;
        hostlist_destroy (hl);
        (*item) = NULL;
    }
}

static struct list_constraint *create_hostlist_constraint (
    struct match_ctx *mctx,
    json_t *values,
    flux_error_t *errp)
{
    struct list_constraint *c;
    struct hostlist *hl = NULL;
    json_t *entry;
    size_t index;

    if (!(c = list_constraint_new (mctx,
                                   match_hostlist,
                                   wrap_hostlist_destroy,
                                   errp)))
        return NULL;
    /* Create a single hostlist if user specifies multiple nodes or
     * RFC29 hostlist range */
    if (!(hl = hostlist_create ())) {
        errprintf (errp, "failed to create hostlist structure");
        goto error;
    }
    json_array_foreach (values, index, entry) {
        if (!json_is_string (entry)) {
            errprintf (errp, "host value must be a string");
            goto error;
        }
        if (hostlist_append (hl, json_string_value (entry)) <= 0) {
            errprintf (errp, "host value not in valid Hostlist format");
            goto error;
        }
    }
    if (hostlist_count (hl) > mctx->max_hostlist) {
        errprintf (errp, "too many hosts specified");
        goto error;
    }
    if (!zlistx_add_end (c->values, hl)) {
        errprintf (errp, "failed to append hostlist structure");
        hostlist_destroy (hl);
        goto error;
    }
    return c;
 error:
    hostlist_destroy (hl);
    list_constraint_destroy (c);
    return NULL;
}

static int match_timestamp (struct list_constraint *c,
                            const struct job *job,
                            unsigned int *comparisons,
                            flux_error_t *errp)
{
    struct timestamp_value *tv = zlistx_first (c->values);
    double t;

    if (inc_check_comparison (c->mctx, comparisons, errp) < 0)
        return -1;

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
            return 0;
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
        return 0;

    if (tv->t_comp == MATCH_GREATER_THAN_EQUAL)
        return t >= tv->t_value;
    else if (tv->t_comp == MATCH_LESS_THAN_EQUAL)
        return t <= tv->t_value;
    else if (tv->t_comp == MATCH_GREATER_THAN)
        return t > tv->t_value;
    else /* tv->t_comp == MATCH_LESS_THAN */
        return t < tv->t_value;
}

static struct list_constraint *create_timestamp_constraint (struct match_ctx *mctx,
                                                            const char *type,
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

    if (!(c = list_constraint_new (mctx,
                                   match_timestamp,
                                   wrap_timestamp_value_destroy,
                                   errp))
        || !zlistx_add_end (c->values, tv)) {
        list_constraint_destroy (c);
        timestamp_value_destroy (tv);
        return NULL;
    }
    return c;
}

static int match_and (struct list_constraint *c,
                      const struct job *job,
                      unsigned int *comparisons,
                      flux_error_t *errp)
{
    struct list_constraint *cp = zlistx_first (c->values);
    while (cp) {
        int ret = cp->match (cp, job, comparisons, errp);
        /* i.e. return immediately if false or error */
        if (ret != 1)
            return ret;
        cp = zlistx_next (c->values);
    }
    return 1;
}

static int match_or (struct list_constraint *c,
                     const struct job *job,
                     unsigned int *comparisons,
                     flux_error_t *errp)
{
    struct list_constraint *cp = zlistx_first (c->values);
    /* no values in "or" defined as true per RFC31 */
    if (!cp)
        return 1;
    while (cp) {
        int ret = cp->match (cp, job, comparisons, errp);
        /* i.e. return immediately if true or error */
        if (ret != 0)
            return ret;
        cp = zlistx_next (c->values);
    }
    return 0;
}

static int match_not (struct list_constraint *c,
                      const struct job *job,
                      unsigned int *comparisons,
                      flux_error_t *errp)
{
    int ret;
    if ((ret = match_and (c, job, comparisons, errp)) < 0)
        return -1;
    return ret ? 0 : 1;
}

static struct list_constraint *conditional_constraint (struct match_ctx *mctx,
                                                       const char *type,
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

    if (!(c = list_constraint_new (mctx,
                                   match_cb,
                                   list_constraint_destructor,
                                   errp)))
        return NULL;

    json_array_foreach (values, index, entry) {
        struct list_constraint *cp = list_constraint_create (mctx, entry, errp);
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

struct list_constraint *list_constraint_create (struct match_ctx *mctx,
                                                json_t *constraint,
                                                flux_error_t *errp)
{
    const char *op;
    json_t *values;

    if (!mctx) {
        errno = EINVAL;
        return NULL;
    }

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
                return create_userid_constraint (mctx, values, errp);
            else if (streq (op, "name"))
                return create_name_constraint (mctx, values, errp);
            else if (streq (op, "queue"))
                return create_queue_constraint (mctx, values, errp);
            else if (streq (op, "states"))
                return create_states_constraint (mctx, values, errp);
            else if (streq (op, "results"))
                return create_results_constraint (mctx, values, errp);
            else if (streq (op, "hostlist"))
                return create_hostlist_constraint (mctx, values, errp);
            else if (streq (op, "t_submit")
                     || streq (op, "t_depend")
                     || streq (op, "t_run")
                     || streq (op, "t_cleanup")
                     || streq (op, "t_inactive"))
                return create_timestamp_constraint (mctx, op, values, errp);
            else if (streq (op, "or") || streq (op, "and") || streq (op, "not"))
                return conditional_constraint (mctx, op, values, errp);
            else {
                errprintf (errp, "unknown constraint operator: %s", op);
                return NULL;
            }
        }
    }
    return list_constraint_new (mctx, match_true, NULL, errp);
}

int job_match (const struct job *job,
               struct list_constraint *constraint,
               flux_error_t *errp)
{
    if (!job || !constraint) {
        errno = EINVAL;
        return -1;
    }
    return constraint->match (constraint, job, &constraint->comparisons, errp);
}

static int config_parse_max_comparisons (struct match_ctx *mctx,
                                         const flux_conf_t *conf,
                                         flux_error_t *errp)
{
    int64_t max_comparisons = CONSTRAINT_COMPARISON_MAX;
    flux_error_t error;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?I}}",
                          "job-list",
                          "max_comparisons", &max_comparisons) < 0) {
        errprintf (errp,
                   "error reading config for job-list: %s",
                   error.text);
        return -1;
    }

    if (max_comparisons < 0) {
        errprintf (errp, "job-list.max_comparisons must be >= 0");
        return -1;
    }

    mctx->max_comparisons = max_comparisons;
    return 0;
}

int job_match_config_reload (struct match_ctx *mctx,
                             const flux_conf_t *conf,
                             flux_error_t *errp)
{
    return config_parse_max_comparisons (mctx, conf, errp);
}

struct match_ctx *match_ctx_create (flux_t *h)
{
    struct match_ctx *mctx = NULL;
    flux_error_t error;

    if (!(mctx = calloc (1, sizeof (*mctx))))
        return NULL;
    mctx->h = h;

    if (config_parse_max_comparisons (mctx,
                                      flux_get_conf (mctx->h),
                                      &error) < 0) {
        flux_log (mctx->h, LOG_ERR, "%s", error.text);
        goto error;
    }

    if (flux_get_size (mctx->h, &mctx->max_hostlist) < 0) {
        flux_log_error (h, "failed to get instance size");
        goto error;
    }

    /* Notes:
     *
     * We do not want a hostlist constraint match to DoS this module.
     * So we want to configure a "max" amount of hosts that can exist
     * within a hostlist constraint.
     *
     * Under normal operating conditions, the number of brokers should
     * represent the most likely maximum.  But there are some corner
     * cases.  For example, the instance gets reconfigured to be
     * smaller, which is not an uncommon thing to do towards a
     * cluster's end of life and hardware is beginning to die.
     *
     * So we configure the following compromise.  If the number of
     * brokers is below our defined minimum MIN_MATCH_HOSTLIST, we'll
     * allow max_hostlist to be increased to this number.
     */
    if (mctx->max_hostlist < MIN_MATCH_HOSTLIST)
        mctx->max_hostlist = MIN_MATCH_HOSTLIST;

    return mctx;

error:
    match_ctx_destroy (mctx);
    return NULL;
}

void match_ctx_destroy (struct match_ctx *mctx)
{
    if (mctx)
        free (mctx);
}

/* vi: ts=4 sw=4 expandtab
 */
