/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* history.c - track jobs in t_submit order per user
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/ptrint/ptrint.h"
#include "ccan/str/str.h"
#include "src/common/libutil/hola.h"
#include "src/common/libutil/slice.h"
#include "src/common/libutil/errprintf.h"

struct job_entry {
    flux_jobid_t id;
    double t_submit;
};

struct history {
    flux_plugin_t *p;
    struct hola *users; // userid => job list
};

#define COMPARE_NUM_REVERSE(a,b) ((a)>(b)?-1:(a)<(b)?1:0)

/* Keys in history->users are calculated from int2ptr(uid), but that won't
 * work for root.  Substitute (uid_t)-1 in that case as it's reserved per
 * POSIX.  See also: flux-framework/flux-core#5475.
 */
static const void *userid2key (int userid)
{
    if (userid == 0)
        return int2ptr ((uid_t)-1);
    return int2ptr (userid);
}

static int key2userid (const void *key)
{
    if (key == int2ptr ((uid_t)-1))
        return 0;
    return ptr2int (key);
}

static void job_entry_destroy (struct job_entry *entry)
{
    if (entry) {
        int saved_errno = errno;
        free (entry);
        errno = saved_errno;
    }
}

static struct job_entry *job_entry_create (void)
{
    struct job_entry *entry;

    if (!(entry = calloc (1, sizeof (*entry))))
        return NULL;
    return entry;
}

// zlistx_destructor_fn footprint
static void job_entry_destructor (void **item)
{
    if (item) {
        job_entry_destroy (*item);
        *item = NULL;
    }
}

// zlistx_comparator_fn footprint
static int job_entry_comparator (const void *item1, const void *item2)
{
    const struct job_entry *a = item1;
    const struct job_entry *b = item2;

    return COMPARE_NUM_REVERSE (a->t_submit, b->t_submit);
}

// zhashx_hash_fn footprint
static size_t userid_hasher (const void *key)
{
    return key2userid (key);
}

// zhashx_comparator_fn footprint
static int userid_comparator (const void *item1, const void *item2)
{
    return COMPARE_NUM_REVERSE (key2userid (item1), key2userid (item2));
}

static void history_destroy (struct history *hist)
{
    if (hist) {
        int saved_errno = errno;
        hola_destroy (hist->users);
        free (hist);
        errno = saved_errno;
    };
}

static struct history *history_create (flux_plugin_t *p)
{
    struct history *hist;

    if (!(hist = calloc (1, sizeof (*hist))))
        return NULL;
    hist->p = p;
    if (!(hist->users = hola_create (HOLA_AUTOCREATE)))
        goto error;
    hola_set_hash_key_destructor (hist->users, NULL);
    hola_set_hash_key_duplicator (hist->users, NULL);
    hola_set_hash_key_comparator (hist->users, userid_comparator);
    hola_set_hash_key_hasher (hist->users, userid_hasher);
    hola_set_list_destructor (hist->users, job_entry_destructor);
    hola_set_list_comparator (hist->users, job_entry_comparator);
    return hist;
error:
    history_destroy (hist);
    return NULL;
}

static int jobtap_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    struct history *hist = arg;
    struct job_entry *entry;
    int userid;
    const void *key;

    if (!(entry = job_entry_create ()))
        return -1;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:f s:i}",
                                "id", &entry->id,
                                "t_submit", &entry->t_submit,
                                "userid", &userid) < 0)
        goto error;
    key = userid2key (userid);
    if (streq (topic, "job.inactive-remove")) {
        void *handle;
        if ((handle = hola_list_find (hist->users, key, entry)))
            hola_list_delete (hist->users, key, handle);
        job_entry_destroy (entry);
    }
    else if (streq (topic, "job.inactive-add")) {
        if (hola_list_find (hist->users, key, entry)) {
            job_entry_destroy (entry);
            return 0;
        }
        if (!hola_list_insert (hist->users, key, entry, true))
            goto error;
    }
    else if (streq (topic, "job.new")) {
        if (!hola_list_insert (hist->users, key, entry, true))
            goto error;
    }
    return 0;
error:
    job_entry_destroy (entry);
    return -1;
}

static int append_int (json_t *a, json_int_t i)
{
    json_t *o;
    if (!(o = json_integer (i))
        || json_array_append_new (a, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

static int list_slice_reverse (zlistx_t *l, struct slice *sl, json_t *a)
{
    if (l) {
        struct job_entry *entry = zlistx_last (l);
        size_t list_index = zlistx_size (l) - 1;
        int slice_index = slice_first (sl);

        while (entry && slice_index != -1) {
            if (list_index == slice_index) {
                if (append_int (a, entry->id) < 0)
                    return -1;
                slice_index = slice_next (sl);
            }
            list_index--;
            entry = zlistx_prev (l);
        }
    }
    return 0;
}

static int list_slice_forward (zlistx_t *l, struct slice *sl, json_t *a)
{
    if (l) {
        struct job_entry *entry = zlistx_first (l);
        size_t list_index = 0;
        int slice_index = slice_first (sl);

        while (entry && slice_index != -1) {
            if (list_index == slice_index) {
                if (append_int (a, entry->id) < 0)
                    return -1;
                slice_index = slice_next (sl);
            }
            list_index++;
            entry = zlistx_next (l);
        }
    }
    return 0;
}

static json_t *history_slice (struct history *hist,
                              int userid,
                              const char *slice,
                              flux_error_t *error)
{
    json_t *a;
    zlistx_t *l;
    struct slice sl;
    size_t list_size = 0;
    int rc;

    if ((l = hola_hash_lookup (hist->users, userid2key (userid))))
        list_size = zlistx_size (l);
    if (slice_parse (&sl, slice, list_size) < 0) {
        errprintf (error, "could not parse python-style slice expression");
        errno = EINVAL;
        return NULL;
    }
    if (!(a = json_array ()))
        goto oom;
    if (sl.step > 0)
        rc = list_slice_forward (l, &sl, a);
    else
        rc = list_slice_reverse (l, &sl, a);
    if (rc < 0)
        goto oom;
    return a;
oom:
    json_decref (a);
    errprintf (error, "out of memory");
    errno = ENOMEM;
    return NULL;
}

static void history_get_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct history *hist = arg;
    const char *slice;
    struct flux_msg_cred cred;
    json_t *jobs;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "slice", &slice) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (!(jobs = history_slice (hist, cred.userid, slice, &error))) {
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "error responding to job-manager.history.get");
    json_decref (jobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.history.get");
}


int history_plugin_init (flux_plugin_t *p)
{
    struct history *hist;

    if (!(hist = history_create (p))
        || flux_plugin_aux_set (p,
                                NULL,
                                hist,
                                (flux_free_f)history_destroy) < 0) {
        history_destroy (hist);
        return -1;
    }
    if (flux_jobtap_service_register_ex (p,
                                         "get",
                                         FLUX_ROLE_USER,
                                         history_get_cb,
                                         hist) < 0)
        return -1;

    if (flux_plugin_add_handler (p,
                                 "job.new",
                                 jobtap_cb,
                                 hist) < 0
        || flux_plugin_add_handler (p,
                                    "job.inactive-add",
                                    jobtap_cb,
                                    hist) < 0
        || flux_plugin_add_handler (p,
                                    "job.inactive-remove",
                                    jobtap_cb,
                                    hist) < 0)
        return -1;

    return 0;
}

// vi:ts=4 sw=4 expandtab
