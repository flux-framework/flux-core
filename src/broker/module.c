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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <json.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#include "module.h"
#include "modhandle.h"
#include "modservice.h"

/* While transitioning to argc, argv - style args per RFC 5,
 * we have our own mod_main prototype.
 */
typedef int (mod_main_comms_f)(flux_t h, zhash_t *args);

#define MOD_MAGIC    0xfeefbe01
struct mod_ctx_struct {
    int magic;

    zctx_t *zctx;
    uint32_t rank;

    void *zs_svc[2];        /* PAIR for handling requests 0=module, 1=broker */
    void *zs_evin;          /* SUB for handling subscribed-to events */

    char *modevent_uri;
    char *svc_uri;

    zuuid_t *uuid;          /* uuid for unique request sender identity */
    pthread_t t;            /* module thread */
    mod_main_comms_f *main; /* dlopened mod_main() */
    const char *name;       /* MOD_NAME */
    void *dso;              /* reference on dlopened module */
    int size;               /* size of .so file for lsmod */
    char *digest;           /* digest of .so file for lsmod */
    size_t argz_len;
    char *argz;

    flux_t h;               /* module's handle */
};

/* FIXME: this should go away once module prototypes are updated.
 */
static zhash_t *zhash_fromargz (mod_ctx_t p)
{
    zhash_t *z;
    char *arg;

    if (!(z = zhash_new ()))
        oom ();
    arg = argz_next (p->argz, p->argz_len, NULL);
    while (arg != NULL) {
        char *key = xstrdup (arg);
        char *val = strchr (key, '=');
        if (!val || strlen (val) == 0)
            val = "1";
        else
            *val++ = '\0';
        zhash_update (z, key, xstrdup (val));
        zhash_freefn (z, key, (zhash_free_fn *)free);
        free (key);
        arg = argz_next (p->argz, p->argz_len, arg);
    }
    return z;
}

static void send_eof (void *sock)
{
    zmsg_t *zmsg;

    if (!(zmsg = zmsg_new ()) || zmsg_pushmem (zmsg, NULL, 0) < 0)
        oom ();
    if (zmsg_send (&zmsg, sock) < 0)
        err_exit ("error sending EOF");
    zmsg_destroy (&zmsg);
}

static void *mod_thread (void *arg)
{
    mod_ctx_t p = arg;
    sigset_t signal_set;
    int errnum;
    zhash_t *args = NULL;

    assert (p->zctx);

    /* Connect to broker sockets
     */
    if (!(p->zs_evin = zsocket_new (p->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->zs_evin, 0);
    if (zsocket_connect (p->zs_evin, "%s", p->modevent_uri) < 0)
        err_exit ("%s", p->modevent_uri);

    if (!(p->zs_svc[0] = zsocket_new (p->zctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->zs_svc[0], 0);
    if (zsocket_connect (p->zs_svc[0], "%s", p->svc_uri) < 0)
        err_exit ("%s", p->svc_uri);

    /* Block all signals
     */
    if (sigfillset (&signal_set) < 0)
        err_exit ("%s: sigfillset", p->name);
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0)
        errn_exit (errnum, "pthread_sigmask");

    /* Create handle, enable logging, register built-in services
     */
    p->h = modhandle_create (p->zs_svc[0], p->zs_evin, zuuid_str (p->uuid),
                             p->rank, p->zctx);
    flux_log_set_facility (p->h, p->name);
    modservice_register (p->h, p->name);

    /* Run the module's main().
     */
    args = zhash_fromargz (p);
    if (p->main(p->h, args) < 0) {
        err ("%s: mod_main returned error", p->name);
        goto done;
    }
done:
    zhash_destroy (&args);

    send_eof (p->zs_svc[0]);

    zsocket_destroy (p->zctx, p->zs_evin);
    zsocket_destroy (p->zctx, p->zs_svc[0]);

    return NULL;
}

const char *mod_name (mod_ctx_t p)
{
    return p->name;
}

const char *mod_uuid (mod_ctx_t p)
{
    return zuuid_str (p->uuid);
}

void *mod_sock (mod_ctx_t p)
{
    return p->zs_svc[1];
}

const char *mod_digest (mod_ctx_t p)
{
    return p->digest;
}

int mod_size (mod_ctx_t p)
{
    return p->size;
}

void mod_destroy (mod_ctx_t p)
{
    int errnum;

    if (p->t) {
        errnum = pthread_join (p->t, NULL);
        if (errnum)
            errn_exit (errnum, "pthread_join");
    }

    flux_handle_destroy (&p->h);

    zsocket_destroy (p->zctx, p->zs_svc[1]);

    dlclose (p->dso);
    zuuid_destroy (&p->uuid);
    free (p->digest);
    if (p->argz)
        free (p->argz);
    if (p->modevent_uri)
        free (p->modevent_uri);
    if (p->svc_uri)
        free (p->svc_uri);

    free (p);
}

/* Send shutdown request, broker to module.
 */
void mod_stop (mod_ctx_t p)
{
    char *topic = xasprintf ("%s.shutdown", p->name);
    zmsg_t *zmsg;
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (zmsg_send (&zmsg, p->zs_svc[1]) < 0)
        goto done;
done:
    free (topic);
    zmsg_destroy (&zmsg);
}

void mod_start (mod_ctx_t p)
{
    int errnum;
    errnum = pthread_create (&p->t, NULL, mod_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create");
}

void mod_set_args (mod_ctx_t p, int argc, char * const argv[])
{
    if (p->argz) {
        free (p->argz);
        p->argz_len = 0;
    }
    if (argv && argz_create (argv, &p->argz, &p->argz_len) < 0)
        oom ();
}

void mod_add_arg (mod_ctx_t p, const char *arg)
{
    if (argz_add (&p->argz, &p->argz_len, arg) < 0)
        oom ();
}

mod_ctx_t mod_create (zctx_t *zctx, uint32_t rank, const char *path)
{
    mod_ctx_t p;
    void *dso;
    const char **mod_namep;
    mod_main_comms_f *mod_main;
    zfile_t *zf;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_LOCAL))) {
        msg ("%s", dlerror ());
        errno = ENOENT;
        return NULL;
    }
    mod_main = dlsym (dso, "mod_main");
    mod_namep = dlsym (dso, "mod_name");
    if (!mod_main || !mod_namep || !*mod_namep) {
        err ("%s: mod_main or mod_name undefined", path);
        dlclose (dso);
        errno = ENOENT;
        return NULL;
    }

    p = xzmalloc (sizeof (*p));
    p->magic = MOD_MAGIC;
    p->main = mod_main;
    p->dso = dso;
    zf = zfile_new (NULL, path);
    p->digest = xstrdup (zfile_digest (zf));
    p->size = (int)zfile_cursize (zf);
    zfile_destroy (&zf);
    p->name = *mod_namep;
    if (!(p->uuid = zuuid_new ()))
        oom ();
    p->rank = rank;


    p->zctx = zctx;

    p->modevent_uri = xstrdup (MODEVENT_INPROC_URI);
    p->svc_uri = xasprintf (SVC_INPROC_URI_TMPL, p->name);

    /* Broker end of service socket pair.
     */
    if (!(p->zs_svc[1] = zsocket_new (zctx, ZMQ_PAIR)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (p->zs_svc[1], 0);
    if (zsocket_bind (p->zs_svc[1], "%s", p->svc_uri) < 0)
        err_exit ("%s", p->svc_uri);
    return p;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
