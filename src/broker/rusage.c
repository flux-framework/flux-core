/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <sys/resource.h>
#include <flux/core.h>
#include <src/common/libutil/shortjson.h>
#include "rusage.h"

struct rusage_context {
    flux_msg_handler_t *w;
};

static int getrusage_json (int who, json_object **op)
{
    struct rusage ru;
    json_object *o;

    if (getrusage (who, &ru) < 0)
        return -1;
    o = Jnew ();
    Jadd_double (o, "utime",
            (double)ru.ru_utime.tv_sec + 1E-6 * ru.ru_utime.tv_usec);
    Jadd_double (o, "stime",
            (double)ru.ru_stime.tv_sec + 1E-6 * ru.ru_stime.tv_usec);
    Jadd_int64 (o, "maxrss",        ru.ru_maxrss);
    Jadd_int64 (o, "ixrss",         ru.ru_ixrss);
    Jadd_int64 (o, "idrss",         ru.ru_idrss);
    Jadd_int64 (o, "isrss",         ru.ru_isrss);
    Jadd_int64 (o, "minflt",        ru.ru_minflt);
    Jadd_int64 (o, "majflt",        ru.ru_majflt);
    Jadd_int64 (o, "nswap",         ru.ru_nswap);
    Jadd_int64 (o, "inblock",       ru.ru_inblock);
    Jadd_int64 (o, "oublock",       ru.ru_oublock);
    Jadd_int64 (o, "msgsnd",        ru.ru_msgsnd);
    Jadd_int64 (o, "msgrcv",        ru.ru_msgrcv);
    Jadd_int64 (o, "nsignals",      ru.ru_nsignals);
    Jadd_int64 (o, "nvcsw",         ru.ru_nvcsw);
    Jadd_int64 (o, "nivcsw",        ru.ru_nivcsw);
    *op = o;
    return 0;
}

static void rusage_request_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    json_object *out = NULL;

    if (getrusage_json (RUSAGE_THREAD, &out) < 0)
        goto error;
    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (out);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void rusage_finalize (void *arg)
{
    struct rusage_context *r = arg;
    flux_msg_handler_stop (r->w);
    flux_msg_handler_destroy (r->w);
    free (r);
}

int rusage_initialize (flux_t *h, const char *service)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct rusage_context *r = calloc (1, sizeof (*r));
    if (!r) {
        errno = ENOMEM;
        goto error;
    }
    match.typemask = FLUX_MSGTYPE_REQUEST;
    if (asprintf (&match.topic_glob, "%s.rusage", service) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!(r->w = flux_msg_handler_create (h, match, rusage_request_cb, r)))
        goto error;
    flux_msg_handler_start (r->w);
    flux_aux_set (h, "flux::rusage", r, rusage_finalize);
    return 0;
error:
    if (r) {
        free (match.topic_glob);
        flux_msg_handler_stop (r->w);
        flux_msg_handler_destroy (r->w);
        free (r);
    }
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
