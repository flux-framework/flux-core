/****************************************************************************\
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/time.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/modules/libmrpc/mrpc.h"

#include "proto.h"

static int unload_mrpc_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = NULL;
    flux_mrpc_t mrpc = NULL;
    const char *json_str;
    JSON in = NULL;
    JSON out = NULL;
    const char *modname;
    int errnum = 0;
    int rc = 0;

    if (flux_event_decode (*zmsg, NULL, &json_str) < 0
                || !(o = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "%s: flux_event_decode: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (!(mrpc = flux_mrpc_create_fromevent (h, o))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "%s: flux_mrpc_create_fromevent: %s",
                      __FUNCTION__, strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (mrpc, &in) < 0)
        errnum = errno;
    else if (modctl_tunload_dec (in, &modname) < 0)
        errnum = EPROTO;
    else if (!strcmp (modname, "modctl") || !strcmp (modname, "kvs"))
        errnum = EINVAL; /* unloading those two would be bad ! */
    else if (flux_rmmod (h, flux_rank (h), modname) < 0)
        errnum = errno;
    //flux_log (h, LOG_DEBUG, "%s: result %d", __FUNCTION__, errnum);
    if ((out = modctl_runload_enc (errnum)))
        flux_mrpc_put_outarg (mrpc, out);
    else
        flux_log (h, LOG_ERR, "%s: modctl_runload_enc: %s",
                  __FUNCTION__, strerror (errno));
    if (flux_mrpc_respond (mrpc) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    Jput (o);
    Jput (in);
    Jput (out);
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    zmsg_destroy (zmsg);
    return rc;
}

static int load_mrpc_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = NULL;
    flux_mrpc_t mrpc = NULL;
    JSON in = NULL;
    JSON out = NULL;
    const char *json_str;
    const char *path;
    int argc = 0;
    const char **argv = NULL;
    int errnum = 0;
    int rc = 0;

    if (flux_event_decode (*zmsg, NULL, &json_str) < 0
                || !(o = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "%s: flux_event_decode: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (!(mrpc = flux_mrpc_create_fromevent (h, o))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "%s: flux_mrpc_create_fromevent: %s",
                      __FUNCTION__, strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (mrpc, &in) < 0)
        errnum = errno;
    else if (modctl_tload_dec (in, &path, &argc, &argv) < 0)
        errnum = EPROTO;
    else if (flux_insmod (h, flux_rank (h), path, argc, (char **)argv) < 0)
        errnum = errno;
    //flux_log (h, LOG_DEBUG, "%s: result %d", __FUNCTION__, errnum);
    if ((out = modctl_rload_enc (errnum)))
        flux_mrpc_put_outarg (mrpc, out);
    else
        flux_log (h, LOG_ERR, "%s: modctl_rload_enc: %s",
                  __FUNCTION__, strerror (errno));
    if (flux_mrpc_respond (mrpc) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    Jput (o);
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    Jput (in);
    if (argv)
        free (argv);
    Jput (out);
    zmsg_destroy (zmsg);
    return rc;
}

static int lsmod_cb (const char *name, int size, const char *digest, int idle,
                     const char *nodeset, void *arg)
{
    JSON o = arg;
    return modctl_rlist_enc_add (o, name, size, digest, idle);
}

static int list_mrpc_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON o = NULL;
    flux_mrpc_t mrpc = NULL;
    JSON in = NULL;
    JSON out = NULL;
    const char *json_str;
    const char *svc;
    int errnum = 0;
    int rc = 0;

    if (flux_event_decode (*zmsg, NULL, &json_str) < 0
                || !(o = Jfromstr (json_str))) {
        flux_log (h, LOG_ERR, "%s: flux_event_decode: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    if (!(mrpc = flux_mrpc_create_fromevent (h, o))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "%s: flux_mrpc_create_fromevent: %s",
                      __FUNCTION__, strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (mrpc, &in) < 0)
        errnum = errno;
    else if (modctl_tlist_dec (in, &svc) < 0)
        errnum = EPROTO;
    else if (!(out = modctl_rlist_enc ()))
        errnum = errno;
    else if (flux_lsmod (h, flux_rank (h), svc, lsmod_cb, out) < 0)
        errnum = errno;
    //flux_log (h, LOG_DEBUG, "%s: result %d", __FUNCTION__, errnum);
    if (!out || modctl_rlist_enc_errnum (out, errnum) < 0) {
        flux_log (h, LOG_ERR, "%s: modctl_rlist_enc: %s",
                  __FUNCTION__, strerror (errno));
    } else
        flux_mrpc_put_outarg (mrpc, out);
    if (flux_mrpc_respond (mrpc) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    Jput (o);
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    Jput (in);
    Jput (out);
    zmsg_destroy (zmsg);
    return rc;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "mrpc.modctl.unload",       unload_mrpc_cb },
    { FLUX_MSGTYPE_EVENT,   "mrpc.modctl.load",         load_mrpc_cb },
    { FLUX_MSGTYPE_EVENT,   "mrpc.modctl.list",         list_mrpc_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, int argc, char **argv)
{
    if (flux_msghandler_addvec (h, htab, htablen, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_event_subscribe (h, "modctl.") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_event_subscribe (h, "mrpc.modctl.") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("modctl");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
