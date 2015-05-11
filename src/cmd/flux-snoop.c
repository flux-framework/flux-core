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
#include <getopt.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/zdump.h"
#include "src/common/libutil/log.h"


#define OPTIONS "hanN:vl"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"all",        no_argument,        0, 'a'},
    {"no-security",no_argument,        0, 'n'},
    {"verbose",    no_argument,        0, 'v'},
    {"long",       no_argument,        0, 'l'},
    {"session-name",required_argument, 0, 'N'},
    { 0, 0, 0, 0 },
};

static char *suppressed[] = { "cmb.info", "cmb.log", "cmb.pub" };

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-snoop OPTIONS [topic [topic...]]\n"
"  -a,--all               Do not suppress cmb.info, cmb.log, cmb.pub\n"
"  -l,--long              Display long message format\n"
#if 0 /* These options are for debugging, not generally useful */
"  -n,--no-security       Try to connect without CURVE security\n"
"  -v,--verbose           Verbose connect output\n"
"  -N,--session-name NAME Set session name (default flux)\n"
#endif
);
    exit (1);
}

static void *connect_snoop (zctx_t *zctx, flux_sec_t sec, const char *uri);
static int snoop_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg);
static int zmon_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg);

static bool aopt = false;
static bool lopt = false;
zlist_t *subscriptions = NULL;

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *uri = NULL;
    bool vopt = false;
    bool nopt = false;
    char *session = "flux";
    zctx_t *zctx;
    void *s;
    zloop_t *zloop;
    zmq_pollitem_t zp;
    flux_sec_t sec;
    int rank = -1;
    const char *secdir;
    int oflags = 0;

    log_init ("flux-snoop");

    if (!(subscriptions = zlist_new ()))
        oom ();
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'a': /* --all */
                aopt = true;
                break;
            case 'l': /* --long */
                lopt = true;
                break;
            case 'n': /* --no-security */
                nopt = true;
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'N': /* --session-name NAME */
                session = optarg;;
                break;
            default:
                usage ();
                break;
        }
    }
    while (optind < argc) {
        if (zlist_append (subscriptions, argv[optind++]) < 0)
            oom ();
    }
    if (!(secdir = getenv ("FLUX_SEC_DIRECTORY")))
        msg_exit ("FLUX_SEC_DIRECTORY is not set");

    if (getenv ("FLUX_HANDLE_TRACE"))
        oflags |= FLUX_O_TRACE;
    if (!(h = flux_open (NULL, oflags)))
        err_exit ("flux_open");
    if (!(uri = flux_getattr (h, rank, "snoop-uri")))
        err_exit ("snoop-uri");

    /* N.B. flux_get_zctx () is not implemented for the API socket since
     * it has no internal zctx (despite supporting the flux reactor).
     */
    if (!(zctx = zctx_new ()))
        err_exit ("zctx_new");
    zctx_set_linger (zctx, 5);

    /* N.B. We use the zloop reactor and handle disconnects via zmonitor.
     * We must handle disconnects, since the default zmq "hidden reconnect"
     * behavior doesn't work across a broker restart, where the dynamically
     * assigned snoop URI may change.
     */
    if (!(zloop = zloop_new ()))
        oom ();

    /* Initialize security ctx.
     */
    if (!(sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    flux_sec_set_directory (sec, secdir);
    if (nopt) {
        if (flux_sec_disable (sec, FLUX_SEC_TYPE_ALL) < 0)
            err_exit ("flux_sec_disable");
        msg ("Security is disabled");
    }
    if (flux_sec_zauth_init (sec, zctx, session) < 0)
        msg_exit ("flux_sec_zinit: %s", flux_sec_errstr (sec));

    /* Connect to the snoop socket
     */
    if (vopt)
        msg ("connecting to %s...", uri);

    if (!(s = connect_snoop (zctx, sec, uri)))
        err_exit ("%s", uri);
    zp.socket = s;
    zp.events = ZMQ_POLLIN;
    if (zloop_poller (zloop, &zp, snoop_cb, NULL) < 0)
        err_exit ("zloop_poller");
    zsocket_set_subscribe (s, "");

    zmonitor_t *zmon;
    if (!(zmon = zmonitor_new (zctx, s, ZMQ_EVENT_DISCONNECTED)))
        err_exit ("zmonitor_new");
    if (vopt)
        zmonitor_set_verbose (zmon, true);
    zp.socket = zmonitor_socket (zmon);
    zp.events = ZMQ_POLLIN;
    if (zloop_poller (zloop, &zp, zmon_cb, NULL) < 0)
        err_exit ("zloop_poller");

    if (zloop_start (zloop) < 0)
        err_exit ("zloop_start");
    if (vopt)
        msg ("disconnecting");

    zmonitor_destroy (&zmon);

    zloop_destroy (&zloop);
    zctx_destroy (&zctx); /* destroys 's' and 'zp.socket' */

    zlist_destroy (&subscriptions);
    free (uri);
    flux_close (h);
    log_fini ();
    return 0;
}

static void *connect_snoop (zctx_t *zctx, flux_sec_t sec, const char *uri)
{
    void *s;

    if (!(s = zsocket_new (zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (sec, s) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (sec));
    if (zsocket_connect (s, "%s", uri) < 0)
        err_exit ("%s", uri);

    return s;
}

static bool suppress (const char *topic)
{
    int i;

    for (i = 0; i < sizeof (suppressed)/sizeof (suppressed[0]); i++)
        if (!strcmp (topic, suppressed[i]))
            return true;
    return false;
}

static bool subscribed (const char *topic)
{
    char *sub;

    if (!(sub = zlist_first (subscriptions)))
        return true;
    while (sub != NULL) {
        int len = strlen (sub);
        if (strlen (topic) >= len && !strncmp (topic, sub, len))
            return true;
        sub = zlist_next (subscriptions);
    }
    return false;
}

static int snoop_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg)
{
    void *zs = item->socket;
    zmsg_t *zmsg;

    if ((zmsg = zmsg_recv (zs))) {
        char *topic = NULL;
        if (flux_msg_get_topic (zmsg, &topic) < 0
                 || (subscribed (topic) && (aopt || !suppress (topic)))) {
            if (lopt) {
                zmsg_dump (zmsg);
            } else {
                const char *pfx = "?";
                int type;
                if (flux_msg_get_type (zmsg, &type) == 0)
                    pfx = flux_msgtype_shortstr (type);
                zdump_fprint (stderr, zmsg, pfx);
            }
        }
        if (topic)
            free (topic);
        zmsg_destroy (&zmsg);
    }
    return 0;
}

static int zmon_cb (zloop_t *zloop, zmq_pollitem_t *item, void *arg)
{
    zmsg_t *zmsg;
    int event = 0;

    if ((zmsg = zmsg_recv (item->socket))) {
        char *s = zmsg_popstr (zmsg);
        if (s) {
            event = strtoul (s, NULL, 10);
            free (s);
        }
        if (event == ZMQ_EVENT_DISCONNECTED)
            msg_exit ("lost connection");
        zmsg_destroy (&zmsg);
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
