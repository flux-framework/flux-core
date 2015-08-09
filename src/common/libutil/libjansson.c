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
#include <stdlib.h>
#include <dlfcn.h>

#include "libjansson.h"
#include "xzmalloc.h"


/* This replicates the inline function json_decref() in <jansson.h>,
 * but uses our dlopened delete function.
 */
void jansson_decref (struct jansson_struct *js, json_t *json)
{
    if (json && json->refcount != (size_t)-1 && --json->refcount == 0)
        js->delete (json);
}

void jansson_destroy (struct jansson_struct *js)
{
    if (js) {
        if (js->dso)
            dlclose (js->dso);
        free (js);
    }
}

struct jansson_struct *jansson_create (void)
{
    struct jansson_struct *js = xzmalloc (sizeof (*js));

    if (!(js->dso = dlopen ("libjansson.so", RTLD_LAZY | RTLD_LOCAL))) {
        char *errstr = dlerror ();
        fprintf (stderr, "%s: %s\n", __FUNCTION__,
                 errstr ? errstr : "dlopen libjansson.so failed");
        jansson_destroy (js);
        return NULL;
    }
    if (!(js->vunpack_ex = dlsym (js->dso, "json_vunpack_ex"))
            || !(js->vpack_ex = dlsym (js->dso, "json_vpack_ex"))
            || !(js->loads = dlsym (js->dso, "json_loads"))
            || !(js->dumps = dlsym (js->dso, "json_dumps"))
            || !(js->delete = dlsym (js->dso, "json_delete"))) {
        char *errstr = dlerror ();
        fprintf (stderr, "%s: %s\n", __FUNCTION__,
                 errstr ? errstr : "dlsym failed");
        jansson_destroy (js);
        return NULL;
    }
    return js;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
