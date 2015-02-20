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
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"

#include "waitqueue.h"

struct cb_args {
    flux_t h;
    int typemask;
    zmsg_t *zmsg;
    void *arg;
};

#define WAIT_MAGIC 0xafad7777
struct wait_struct {
    int magic;
    char *id;
    int usecount;
    FluxMsgHandler cb;
    struct cb_args cb_args;
    struct timespec t0;
};

#define WAITQUEUE_MAGIC 0xafad7778
struct waitqueue_struct {
    int magic;
    zlist_t *q;
};

wait_t wait_create (flux_t h, int typemask, zmsg_t **zmsg,
                     FluxMsgHandler cb, void *arg)
{
    wait_t w = xzmalloc (sizeof (*w));
    w->magic = WAIT_MAGIC;
    w->cb_args.h = h;
    w->cb_args.typemask = typemask;
    w->cb_args.zmsg = *zmsg;
    *zmsg = NULL;                   /* we take over ownership */
    w->cb_args.arg = arg; 
    w->cb = cb;
    monotime (&w->t0);

    return w;
}

void wait_destroy (wait_t w, zmsg_t **zmsg, double *msec)
{
    assert (w->magic == WAIT_MAGIC);
    assert (zmsg != NULL || w->cb_args.zmsg == NULL);
    if (msec)
        *msec = monotime_since (w->t0);
    if (zmsg) {
        *zmsg = w->cb_args.zmsg;    /* we give back ownership */
        w->cb_args.zmsg = NULL;
    }
    if (w->cb_args.zmsg)
        zmsg_destroy (&w->cb_args.zmsg);
    if (w->id)
        free (w->id);
    w->magic = ~WAIT_MAGIC;
    free (w);
}

waitqueue_t wait_queue_create (void)
{
    waitqueue_t q = xzmalloc (sizeof (*q));
    if (!(q->q = zlist_new ()))
        oom ();
    q->magic = WAITQUEUE_MAGIC;
    return q;
}

void wait_queue_destroy (waitqueue_t q)
{
    wait_t w;
    zmsg_t *zmsg;

    assert (q->magic == WAITQUEUE_MAGIC);
    while ((w = zlist_pop (q->q))) {
        wait_destroy (w, &zmsg, NULL);
        zmsg_destroy (&zmsg);
    }
    zlist_destroy (&q->q);
    q->magic = ~WAITQUEUE_MAGIC;
    free (q);
}

int wait_queue_length (waitqueue_t q)
{
    assert (q->magic == WAITQUEUE_MAGIC);
    return zlist_size (q->q);
}

void wait_addqueue (waitqueue_t q, wait_t w)
{
    assert (q->magic == WAITQUEUE_MAGIC);
    assert (w->magic == WAIT_MAGIC);
    if (zlist_append (q->q, w) < 0)
        oom ();
    w->usecount++;
}

void wait_runone (wait_t w)
{
    assert (w->magic == WAIT_MAGIC);

    if (--w->usecount == 0) {
        w->cb (w->cb_args.h, w->cb_args.typemask, &w->cb_args.zmsg,
               w->cb_args.arg);
        w->cb_args.zmsg = NULL; /* ownership transferred to callback */
        wait_destroy (w, NULL, NULL);
    }
}

void wait_runqueue (waitqueue_t q)
{
    zlist_t *cpy;
    wait_t w;

    assert (q->magic == WAITQUEUE_MAGIC);
    if (!(cpy = zlist_new ()))
        oom ();
    while ((w = zlist_pop (q->q))) {
        if (zlist_append (cpy, w) < 0)
            oom ();
    }
    while ((w = zlist_pop (cpy)))
        wait_runone (w);
    zlist_destroy (&cpy);
}

int wait_destroy_match (waitqueue_t q, WaitCompareFn cb, void *arg)
{
    zlist_t *tmp;
    wait_t w;
    zmsg_t *zmsg;
    int rc = -1;

    assert (q->magic == WAITQUEUE_MAGIC);
    if (!(tmp = zlist_new ()))
        oom ();

    w = zlist_first (q->q);
    while (w) {
        if (w->cb_args.zmsg && cb != NULL && cb (w->cb_args.zmsg, arg)) {
            if (zlist_append (tmp, w) < 0)
                oom ();
        }
        w = zlist_next (q->q);
    }
    if (zlist_size (tmp) == 0) {
        errno = ENOENT;
        goto done;
    }
    while ((w = zlist_pop (tmp))) {
        zlist_remove (q->q, w);
        wait_destroy (w, &zmsg, NULL);
        zmsg_destroy (&zmsg);
    }
    rc = 0;
done:
    zlist_destroy (&tmp);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

