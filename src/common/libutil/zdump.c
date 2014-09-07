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

#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "jsonutil.h"
#include "xzmalloc.h"

static int hopcount (zmsg_t *zmsg)
{
    int count = 0;
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        count++;
        zf = zmsg_next (zmsg); /* skip non-empty */
    }
    if (!zf)
        count = 0;
    return count;
}

char *zdump_routestr (zmsg_t *self, int skiphops)
{
    int len = 1, hops = hopcount (self) - skiphops;
    zframe_t *zf = zmsg_first (self);
    char *buf;
    zlist_t *ids;
    char *s;

    if (!(ids = zlist_new ()))
        oom ();
    while (hops-- > 0) {
        if (!(s = zframe_strdup (zf)))
            oom ();
        if (strlen (s) == 32) /* abbreviate long uuids */
            s[5] = '\0';
        if (zlist_push (ids, s) < 0)
            oom ();
        len += strlen (s) + 1;
        zf = zmsg_next (self);
    }
    buf = xzmalloc (len);
    while ((s = zlist_pop (ids))) {
        int l = strlen (buf);
        snprintf (buf + l, len - l, "%s%s", l > 0 ? "!" : "", s);
        free (s);
    }
    zlist_destroy (&ids);
    return buf;
}

/* borrowed zfram_print from czmq and modified */
void zdump_fprint (FILE *f, zmsg_t *self, const char *prefix)
{
    int hops;
    zframe_t *zf;

    fprintf (f, "--------------------------------------\n");
    if (!self) {
        fprintf (f, "NULL");
        return;
    }
    hops = hopcount (self);
    if (hops > 0) {
        char *rte = zdump_routestr (self, 0);
        fprintf (f, "%s[%3.3d] |%s|\n", prefix ? prefix : "", hops, rte);

        zf = zmsg_first (self);
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (self);
        if (zf)
            zf = zmsg_next (self); // skip empty delimiter frame
        free (rte);
    } else {
        zf = zmsg_first (self);
    }
    while (zf) {
        zframe_fprint (zf, prefix ? prefix : "", f);
        zf = zmsg_next (self);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

