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

/* forkzio.c - run process with stdio connected to a zmq PAIR socket */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <json/json.h>
#include <assert.h>
#include <sys/wait.h>
#include <termios.h>
#include <pty.h>
#include <czmq.h>
#include <json/json.h>

#include "log.h"
#include "xzmalloc.h"

#include "flux.h"

#include "zio.h"
#include "forkzio.h"

struct forkzio_handle_struct {
    int ac;
    char **av;
    int readers;
    zctx_t *zctx;
    void *zs;
    int flags;
    zio_t zio[3];
};

/* Data is ready on the zmq pair socket.
 * Look for a zio matching the stream name and send it.
 */
static int forkzio_zsock_cb (zloop_t *zl, zmq_pollitem_t *zp, void *arg)
{
    forkzio_t ctx = arg;
    json_object *o = NULL;
    char *buf = NULL;
    zmsg_t *zmsg;
    int i, rc = -1;
    char *stream = NULL;

    if (!(zmsg = zmsg_recv (zp->socket)))
        goto done;
    if (!(stream = zmsg_popstr (zmsg)))
        goto done;
    if (!(buf = zmsg_popstr (zmsg)))
        goto done;
    if (!(o = json_tokener_parse (buf)))
        goto done;
    for (i = 0; i < 3; i++) {
        if (!ctx->zio[i] || strcmp (zio_name (ctx->zio[i]), stream) != 0)
            continue;
        if ((ctx->flags & FORKZIO_FLAG_DEBUG))
            msg ("%s: msg %s => zio[%d]", __FUNCTION__, stream, i);
        if (zio_write_json (ctx->zio[i], o) < 0) {
            err ("zio_write_json");
            goto done;
        }
        break;
    }
    /* N.B. if we wrote json containing only the eof and no data,
     * our close callback will be called but from zio_write_json(),
     * not from the reactor, so a -1 return from it won't cause the
     * reactor to exit.  Therefore, we have to catch the termination
     * condition here (no more readers) and return -1 to the zloop.
     */
    rc = ctx->readers > 0 ? 0 : -1;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (buf)
        free (buf);
    if (o)
        json_object_put (o);
    if (stream)
        free (stream);
    return rc;
}

static int forkzio_close_cb (zio_t zio, void *arg)
{
    forkzio_t ctx = arg;

    ctx->readers--;
    if ((ctx->flags & FORKZIO_FLAG_DEBUG))
        msg ("%s: closing %s, %d readers left", __FUNCTION__,
             zio_name (zio), ctx->readers);

    return (ctx->readers > 0 ? 0 : -1); /* exit zloop when readers == 0 */
}

static pid_t forkzio_fork (forkzio_t ctx)
{
    pid_t pid;

    switch ((pid = fork ())) {
        case -1: /* error */
            err_exit ("fork");
        case 0: /* child */
            close (STDIN_FILENO);
            dup2 (zio_src_fd (ctx->zio[0]), STDIN_FILENO);
            close (zio_src_fd (ctx->zio[0]));
            close (zio_dst_fd (ctx->zio[0]));

            close (STDOUT_FILENO);
            dup2 (zio_dst_fd (ctx->zio[1]), STDOUT_FILENO);
            close (zio_dst_fd (ctx->zio[1]));
            close (zio_src_fd (ctx->zio[1]));

            close (STDERR_FILENO);
            dup2 (zio_dst_fd (ctx->zio[2]), STDERR_FILENO);
            close (zio_dst_fd (ctx->zio[2]));
            close (zio_src_fd (ctx->zio[2]));

            (void)execvp (ctx->av[0], ctx->av);
            err_exit ("%s", ctx->av[0]);
        default: /* parent */
            close (zio_src_fd (ctx->zio[0]));
            close (zio_dst_fd (ctx->zio[1]));
            close (zio_dst_fd (ctx->zio[2]));
            break;
    }
    return pid;
}

static void forkzio_wait (pid_t pid)
{
    int s;

    if (waitpid (pid, &s, 0) < 0)
        err_exit ("waitpid");
    if (WIFEXITED (s)) {
        int rc = WEXITSTATUS (s);
        if (rc == 0)
            msg ("Child exited normally.");
        else
            msg ("Child exited with %d", rc);
    } else if (WIFSIGNALED (s)) {
        msg ("Child exited on signal %d%s", WTERMSIG (s),
             WCOREDUMP (s) ? " (core dumped)" : "");
    } else if (WIFSTOPPED (s)) {
        msg ("Stopped.");
    } else if (WIFCONTINUED (s)) {
        msg ("Continued.");
    }
}

static void forkzio_pipe_thd (void *args, zctx_t *zctx, void *zs)
{
    forkzio_t ctx = args;
    zmq_pollitem_t zp = { .fd = -1, .socket = zs, .events = ZMQ_POLLIN };
    zloop_t *zloop;
    pid_t pid;

    if (!(zloop = zloop_new ()))
        oom ();

    /* child stdin <= zs
     */
    zloop_poller (zloop, &zp, (zloop_fn *)forkzio_zsock_cb, ctx);
    ctx->zio[0] = zio_pipe_writer_create ("stdin", NULL);
    if (zio_zloop_attach (ctx->zio[0], zloop) < 0)
        err_exit ("zio_zloop_attach %s", zio_name (ctx->zio[0]));

    /* child stdout => zs
     */
    ctx->zio[1] = zio_pipe_reader_create ("stdout", zs, ctx);
    zio_set_close_cb (ctx->zio[1], forkzio_close_cb);
    if (zio_zloop_attach (ctx->zio[1], zloop) < 0)
        err_exit ("zio_zloop_attach %s", zio_name (ctx->zio[1]));
    ctx->readers++;

    /* child stderr => zs
     */
    ctx->zio[2] = zio_pipe_reader_create ("stderr", zs, ctx);
    zio_set_close_cb (ctx->zio[2], forkzio_close_cb);
    if (zio_zloop_attach (ctx->zio[2], zloop) < 0)
        err_exit ("zio_zloop_attach %s", zio_name (ctx->zio[2]));
    ctx->readers++;

    pid = forkzio_fork (ctx);
    (void)zloop_start (zloop);
    forkzio_wait (pid);

    zio_destroy (ctx->zio[0]);
    zio_destroy (ctx->zio[1]);
    zio_destroy (ctx->zio[2]);

    zstr_send (zs, ""); /* signify EOF by sending an empty message */
}

static void forkzio_pty_thd (void *args, zctx_t *zctx, void *zs)
{
    forkzio_t ctx = args;
    zmq_pollitem_t zp = { .fd = -1, .socket = zs, .events = ZMQ_POLLIN };
    zloop_t *zloop;
    pid_t pid;
    int ptyfd;

    if (!(zloop = zloop_new ()))
        oom ();

    switch ((pid = forkpty (&ptyfd, NULL, NULL, NULL))) {
        case -1: /* error */
            err_exit ("forkpty");
        case 0: /* child */
            (void)execvp (ctx->av[0], ctx->av);
            err_exit ("%s", ctx->av[0]);
        default: /* parent */
            break;
    }

    /* Data read from zs is written to pty master
     */
    zloop_poller (zloop, &zp, (zloop_fn *)forkzio_zsock_cb, ctx);
    ctx->zio[0] = zio_writer_create ("stdin", ptyfd, NULL);
    zio_set_unbuffered (ctx->zio[0]);
    if (zio_zloop_attach (ctx->zio[0], zloop) < 0)
        err_exit ("zio_zloop_attach %s", zio_name (ctx->zio[0]));

    /* Data read from pty master is written to zs
     */
    ctx->zio[1] = zio_reader_create ("stdout", ptyfd, zs, ctx);
    zio_set_unbuffered (ctx->zio[1]);
    zio_set_close_cb (ctx->zio[1], forkzio_close_cb);
    if (zio_zloop_attach (ctx->zio[1], zloop) < 0)
        err_exit ("zio_zloop_attach %s", zio_name (ctx->zio[1]));
    ctx->readers++;

    (void)zloop_start (zloop);
    forkzio_wait (pid);

    zio_destroy (ctx->zio[0]);
    zio_destroy (ctx->zio[1]);

    zstr_send (zs, ""); /* signify EOF by sending an empty message */
}

forkzio_t forkzio_open (zctx_t *zctx, int ac, char **av, int flags)
{
    zthread_attached_fn *thd = forkzio_pipe_thd;
    forkzio_t ctx = xzmalloc (sizeof (*ctx));

    ctx->ac = ac;
    ctx->av = av;
    ctx->zctx = zctx;
    ctx->flags = flags;

    if ((ctx->flags & FORKZIO_FLAG_PTY)) 
        thd = forkzio_pty_thd;
    if (!(ctx->zs = zthread_fork (zctx, thd, ctx))) {
        free (ctx);
        ctx = NULL;
    }
    return ctx;
}

void forkzio_close (forkzio_t ctx)
{
    zmq_close (ctx->zs);
    free (ctx);
}

void *forkzio_get_zsocket (forkzio_t ctx)
{
    return ctx->zs;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
