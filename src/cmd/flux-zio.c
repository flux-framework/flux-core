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
#include <assert.h>
#include <sys/wait.h>
#include <termios.h>
#include <stdbool.h>
#include <czmq.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libzio/zio.h"
#include "src/common/libzio/kz.h"
#include "src/common/libzio/forkzio.h"


typedef struct {
    flux_t h;
    void *zs;
    kz_t kz[3];
    int readers;
    int blocksize;
} ctx_t;

static void copy (flux_t h, const char *src, const char *dst, bool trunc,
                  bool lazy, int blocksize);
static void attach (flux_t h, const char *key, int flags, bool trunc,
                   bool lazy, int blocksize);
static void run (flux_t h, const char *key, int ac, char **av, int flags,
                 bool trunc, bool lazy);

#define OPTIONS "hra:cpk:dfb:l"
static const struct option longopts[] = {
    {"help",         no_argument,        0, 'h'},
    {"run",          no_argument,        0, 'r'},
    {"attach",       required_argument,  0, 'a'},
    {"copy",         no_argument,        0, 'c'},
    {"key",          required_argument,  0, 'k'},
    {"pty",          no_argument,        0, 'p'},
    {"debug",        no_argument,        0, 'd'},
    {"force",        no_argument,        0, 'f'},
    {"lazy",         no_argument,        0, 'l'},
    {"blocksize",    required_argument,  0, 'b'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-zio [OPTIONS] --run CMD ...\n"
"       flux-zio [OPTIONS] --attach NAME\n"
"       flux-zio [OPTIONS] --copy from to\n"
"Where OPTIONS are:\n"
"  -k,--key NAME         run with stdio attached to the specified KVS dir\n"
"  -p,--pty              run/attach using a pty\n"
"  -f,--force            truncate KVS on write\n"
"  -b,--blocksize BYTES  set stdin blocksize (default 4096)\n"
"  -l,--lazy             flush data to KVS lazily (defer commit until close)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    bool copt = false;
    bool aopt = false;
    bool ropt = false;
    bool fopt = false;
    bool lopt = false;
    char *key = NULL;
    int blocksize = 4096;
    int flags = 0;
    flux_t h;

    log_init ("flux-zio");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'a': /* --attach NAME */
                aopt = true;
                key = xstrdup (optarg);
                break;
            case 'c': /* --copy */
                copt = true;
                break;
            case 'r': /* --run */
                ropt = true;
                break;
            case 'k': /* --key NAME */
                key = xstrdup (optarg);
                break;
            case 'p': /* --pty */
                flags |= FORKZIO_FLAG_PTY;
                break;
            case 'd': /* --debug */
                flags |= FORKZIO_FLAG_DEBUG;
                break;
            case 'f': /* --force */
                fopt = true;
                break;
            case 'l': /* --lazy */
                lopt = true;
                break;
            case 'b': /* --blocksize bytes */
                blocksize = strtoul (optarg, NULL, 10);
                break;
            default:  /* --help|? */
                usage ();
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (!ropt && !aopt && !copt)
        usage ();
    if (ropt) {
        if (argc == 0)
            usage ();
    } else if (copt) {
        if (argc != 2)
            usage ();
    } else {
        if (argc != 0)
            usage ();
    }

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    if ((aopt || ropt) && !key) {
        if (asprintf (&key, "zio.%d.%d", flux_rank (h), (int)getpid ()) < 0)
            oom ();
    }

    if (aopt) {
        attach (h, key, flags, fopt, lopt, blocksize);
    } else if (ropt) {
        run (h, key, argc, argv, flags, fopt, lopt);
    } else if (copt) {
        copy (h, argv[0], argv[1], fopt, lopt, blocksize);
    }

    flux_close (h);

    free (key);
    log_fini ();
    return 0;
}

static int run_send_kz (kz_t *kzp, char *data, int len, bool eof)
{
    int rc = -1;

    if (!*kzp) {
        errno = EPROTO;
        goto done;
    }
    if (len > 0 && data != NULL) {
        if (kz_put (*kzp, data, len) < 0)
            goto done;
    }
    if (eof) {
        if (kz_close (*kzp) < 0)
            goto done;
        *kzp = NULL;
    }
    rc = 0;
done:
    return rc;
}

static json_object *run_recv_zs (void *zs, char **streamp)
{
    zmsg_t *zmsg = zmsg_recv (zs);
    json_object *o = NULL;
    char *buf = NULL;
    char *stream = NULL;

    if (!zmsg || !(stream = zmsg_popstr (zmsg)) || strlen (stream) == 0
              || !(buf = zmsg_popstr (zmsg))    || strlen (buf) == 0)
        goto done;
    if (!(o = json_tokener_parse (buf)))
        goto done;
    *streamp = stream;
done:
    if (!o && stream)
        free (stream);
    if (buf)
        free (buf);
    if (zmsg)
        zmsg_destroy (&zmsg);
    return o;
}

static int run_zs_ready_cb (flux_t h, void *zs, short revents, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o;
    char *stream = NULL;
    bool eof;
    char *data = NULL;
    int len = 0;
    int rc = -1;

    if (!(o = run_recv_zs (zs, &stream))) {
        flux_reactor_stop (h);
        rc = 0;
        goto done;
    }
    if ((len = zio_json_decode (o, (void **) &data, &eof)) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (!strcmp (stream, "stdout")) {
        if (run_send_kz (&ctx->kz[1], data, len, eof) < 0)
            goto done;
    } else if (!strcmp (stream, "stderr")) {
        if (run_send_kz (&ctx->kz[2], data, len, eof) < 0)
            goto done;
    } else {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    if (data)
        free (data);
    if (stream)
        free (stream);
    if (o)
        json_object_put (o);
    return rc;
}

static int run_send_zs (void *zs, json_object *o, char *stream)
{
    zmsg_t *zmsg;
    const char *s;
    int rc = -1;

    if (!(zmsg = zmsg_new ()))
        oom ();
    s = json_object_to_json_string (o);
    if (zmsg_pushstr (zmsg, s) < 0)
        goto done;
    if (zmsg_pushstr (zmsg, stream) < 0)
        goto done;
    if (zmsg_send (&zmsg, zs) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

static void run_stdin_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    int len;
    char *data;
    json_object *o;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stdin");
        } else if (len > 0) {
            if (!(o = zio_json_encode (data, len, false)))
                err_exit ("zio_json_encode");
            if (run_send_zs (ctx->zs, o, "stdin") < 0)
                err_exit ("run_send_zs");
            free (data);
            json_object_put (o);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (!(o = zio_json_encode (NULL, 0, true)))
            err_exit ("zio_json_encode");
        if (run_send_zs (ctx->zs, o, "stdin") < 0)
            err_exit ("run_send_zs");
        json_object_put (o);
    }
}

static void run (flux_t h, const char *key, int ac, char **av, int flags,
                 bool trunc, bool lazy)
{
    zctx_t *zctx = zctx_new ();
    forkzio_t fz;
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    char *name;
    int kzoutflags = KZ_FLAGS_WRITE;

    if (trunc)
        kzoutflags |= KZ_FLAGS_TRUNC;
    if (lazy)
        kzoutflags |= KZ_FLAGS_DELAYCOMMIT;

    ctx->h = h;

    msg ("process attached to %s", key);

    if (!(fz = forkzio_open (zctx, ac, av, flags)))
        err_exit ("forkzio_open");
    ctx->zs = forkzio_get_zsocket (fz);
    if (flux_zshandler_add (ctx->h, ctx->zs, ZMQ_POLLIN,
                            run_zs_ready_cb, ctx) < 0)
        err_exit ("flux_zshandler_add"); 

    if (asprintf (&name, "%s.stdin", key) < 0)
        oom ();
    ctx->kz[0] = kz_open (h, name,
                          KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK | KZ_FLAGS_NOEXIST);
    if (!ctx->kz[0])
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[0], run_stdin_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);

    if (asprintf (&name, "%s.stdout", key) < 0)
        oom ();
    ctx->kz[1] = kz_open (h, name, kzoutflags);
    if (!ctx->kz[1])
        err_exit ("kz_open %s", name);
    free (name);

    if (asprintf (&name, "%s.stderr", key) < 0)
        oom ();
    ctx->kz[2] = kz_open (h, name, kzoutflags);
    if (!ctx->kz[2])
        err_exit ("kz_open %s", name);
    free (name);

    if (flux_reactor_start (ctx->h) < 0)
        err_exit ("flux_reactor_start");
    forkzio_close (fz);

    (void)kz_close (ctx->kz[0]);

    zmq_term (zctx);
    free (ctx);
}

static int fd_set_raw (int fd, struct termios *tio_save, bool goraw)
{
    struct termios tio;

    if (goraw) { /* save */
        if (tcgetattr (STDIN_FILENO, &tio) < 0)
            return -1;
        *tio_save = tio;
        cfmakeraw (&tio);
        if (tcsetattr (STDIN_FILENO, TCSANOW, &tio) < 0)
            return -1;
    } else { /* restore */
        if (tcsetattr (STDIN_FILENO, TCSANOW, tio_save) < 0)
            return -1;
    }
    return 0;
}

static int fd_set_nonblocking (int fd, bool nonblock)
{
    int fval;

    assert (fd >= 0);

    if ((fval = fcntl (fd, F_GETFL, 0)) < 0)
        return (-1);
    if (nonblock)
        fval |= O_NONBLOCK;
    else
        fval &= ~O_NONBLOCK;
    if (fcntl (fd, F_SETFL, fval) < 0)
        return (-1);
    return (0);
}

static int write_all (int fd, char *buf, int len)
{
    int n, count = 0;

    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
}

static void attach_stdout_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    char *data;
    int len;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stdout");
        } else if (len > 0) {
            if (write_all (STDOUT_FILENO, data, len) < 0)
                err_exit ("write_all stdout");
            free (data);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (ctx->h);
    }
}

static void attach_stderr_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    int len;
    char *data;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stderr");
        } else if (len > 0) {
            if (write_all (STDERR_FILENO, data, len) < 0)
                err_exit ("write_all stderr");
            free (data);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (ctx->h);
    }
}

static int attach_stdin_ready_cb (flux_t h, int fd, short revents, void *arg)
{
    ctx_t *ctx = arg;
    char *buf = xzmalloc (ctx->blocksize);
    int len;

    do  {
        if ((len = read (fd, buf, ctx->blocksize)) < 0) {
            if (errno != EAGAIN)
                err_exit ("read stdin");
        } else if (len > 0) {
            if (kz_put (ctx->kz[0], buf, len) < 0)
                err_exit ("kz_put");
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (kz_close (ctx->kz[0]) < 0)
            err_exit ("kz_close");
    }
    free (buf);
    return 0;
}

static void attach (flux_t h, const char *key, int flags, bool trunc,
                    bool lazy, int blocksize)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    char *name;
    int fdin = dup (STDIN_FILENO);
    struct termios saved_tio;
    int kzoutflags = KZ_FLAGS_WRITE;

    if (trunc)
        kzoutflags |= KZ_FLAGS_TRUNC;
    if (lazy)
        kzoutflags |= KZ_FLAGS_DELAYCOMMIT;

    msg ("process attached to %s", key);

    ctx->h = h;
    ctx->blocksize = blocksize;

    /* FIXME: need a ~. style escape sequence to terminate stdin
     * in raw mode.
     */
    if ((flags & FORKZIO_FLAG_PTY)) {
        if (fd_set_raw (fdin, &saved_tio, true) < 0)
            err_exit ("fd_set_raw stdin");
    }
    if (fd_set_nonblocking (fdin, true) < 0)
        err_exit ("fd_set_nonblocking stdin");

    if (asprintf (&name, "%s.stdin", key) < 0)
        oom ();
    if (!(ctx->kz[0] = kz_open (h, name, kzoutflags)))
        if (errno == EEXIST)
            err ("disabling stdin");
        else
            err_exit ("%s", name);
    else {
        if (flux_fdhandler_add (h, fdin, ZMQ_POLLIN | ZMQ_POLLERR,
                                attach_stdin_ready_cb, ctx) < 0)
            err_exit ("flux_fdhandler_add %s", name);
    }
    free (name);

    if (asprintf (&name, "%s.stdout", key) < 0)
        oom ();
    if (!(ctx->kz[1] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[1], attach_stdout_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    if (asprintf (&name, "%s.stderr", key) < 0)
        oom ();
    if (!(ctx->kz[2] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[2], attach_stderr_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    /* Reactor terminates when ctx->readers reaches zero, i.e.
     * when EOF is read from remote stdout and stderr.
     * (Note: if they are already at eof, we will have already terminated
     * before the reactor is started, since kvs_watch callbacks make one
     * call to the callback in the context of the caller).
     */
    if (ctx->readers > 0) {
        if (flux_reactor_start (ctx->h) < 0)
            err_exit ("flux_reactor_start");
    }

    (void)kz_close (ctx->kz[1]);
    (void)kz_close (ctx->kz[2]);

    /* FIXME: tty state needs to be restored on all exit paths.
     */
    if ((flags & FORKZIO_FLAG_PTY)) {
        if (fd_set_raw (fdin, &saved_tio, false) < 0)
            err_exit ("fd_set_raw stdin");
    }

    free (ctx);
}

static void copy_k2k (flux_t h, const char *src, const char *dst, bool trunc,
                      bool lazy)
{
    int kzoutflags = KZ_FLAGS_WRITE | KZ_FLAGS_RAW;
    kz_t kzin, kzout;
    json_object *val;
    bool eof = false;

    if (trunc)
        kzoutflags |= KZ_FLAGS_TRUNC;
    if (lazy)
        kzoutflags |= KZ_FLAGS_DELAYCOMMIT;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ | KZ_FLAGS_RAW)))
        err_exit ("kz_open %s", src);
    if (!(kzout = kz_open (h, dst, kzoutflags)))
        err_exit ("kz_open %s", dst);
    while (!eof && (val = kz_get_json (kzin))) {
        if (kz_put_json (kzout, val) < 0)
            err_exit ("kz_put_json %s", dst);
        eof = zio_json_eof (val);
        json_object_put (val);
    }
    if (val == NULL)
        err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        err_exit ("kz_close %s", src);
    if (kz_close (kzout) < 0) 
        err_exit ("kz_close %s", dst);
}

static void copy_f2k (flux_t h, const char *src, const char *dst, bool trunc,
                      bool lazy, int blocksize)
{
    int srcfd = STDIN_FILENO;
    int kzoutflags = KZ_FLAGS_WRITE;
    kz_t kzout;
    char *data;
    int len;

    if (trunc)
        kzoutflags |= KZ_FLAGS_TRUNC;
    if (lazy)
        kzoutflags |= KZ_FLAGS_DELAYCOMMIT;

    if (strcmp (src, "-") != 0) {
        if ((srcfd = open (src, O_RDONLY)) < 0)
            err_exit ("%s", src);
    }
    if (!(kzout = kz_open (h, dst, kzoutflags)))
        err_exit ("kz_open %s", dst);
    data = xzmalloc (blocksize);
    while ((len = read (srcfd, data, blocksize)) > 0) {
        if (kz_put (kzout, data, len) < 0)
            err_exit ("kz_put %s", dst);
    }
    if (len < 0)
        err_exit ("read %s", src);
    free (data);
    if (kz_close (kzout) < 0) 
        err_exit ("kz_close %s", dst);
}

static void copy_k2f (flux_t h, const char *src, const char *dst)
{
    kz_t kzin;
    int dstfd = STDOUT_FILENO;
    char *data;
    int len;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ)))
        err_exit ("kz_open %s", src);
    if (strcmp (dst, "-") != 0) {
        if ((dstfd = creat (dst, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
            err_exit ("%s", dst);
    }
    while ((len = kz_get (kzin, &data)) > 0) {
        if (write_all (dstfd, data, len) < 0)
            err_exit ("write_all %s", dst);
        free (data);
    }
    if (len < 0)
        err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        err_exit ("kz_close %s", src);
    if (dstfd != STDOUT_FILENO) {
        if (close (dstfd) < 0)
            err_exit ("close %s", dst);
    }
}

static bool isfile (const char *name)
{
    return (!strcmp (name, "-") || strchr (name, '/'));
}

static void copy (flux_t h, const char *src, const char *dst, bool trunc,
                  bool lazy, int blocksize)
{
    if (!isfile (src) && !isfile (dst)) {
        copy_k2k (h, src, dst, trunc, lazy);
    } else if (isfile (src) && !isfile (dst)) {
        copy_f2k (h, src, dst, trunc, lazy, blocksize);
    } else if (!isfile (src) && isfile (dst)) {
        copy_k2f (h, src, dst);
    } else {
        err_exit ("copy src and dst cannot both be file");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
