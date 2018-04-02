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
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libsubprocess/zio.h"
#include "src/common/libkz/kz.h"


typedef struct {
    flux_t *h;
    void *zs;
    kz_t *kz[3];
    int readers;
    int blocksize;
} t_kzutil_ctx_t;

static void copy (flux_t *h, const char *src, const char *dst, int kzoutflags,
                  int blocksize);
static void attach (flux_t *h, const char *key, bool raw, int kzoutflags,
                   int blocksize);

#define OPTIONS "ha:crk:b:d"
static const struct option longopts[] = {
    {"help",         no_argument,        0, 'h'},
    {"attach",       required_argument,  0, 'a'},
    {"copy",         no_argument,        0, 'c'},
    {"key",          required_argument,  0, 'k'},
    {"raw-tty",      no_argument,        0, 'r'},
    {"delay-commit", no_argument,        0, 'd'},
    {"blocksize",    required_argument,  0, 'b'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: kzutil [OPTIONS] --attach NAME\n"
"       kzutil [OPTIONS] --copy from to\n"
"Where OPTIONS are:\n"
"  -k,--key NAME         stdio should use the specified KVS dir\n"
"  -r,--raw-tty          attach tty in raw mode\n"
"  -b,--blocksize BYTES  set stdin blocksize (default 4096)\n"
"  -d,--delay-commit     flush data to KVS lazily (defer commit until close)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    bool copt = false;
    bool aopt = false;
    char *key = NULL;
    int blocksize = 4096;
    int kzoutflags = KZ_FLAGS_WRITE;
    flux_t *h;
    uint32_t rank;
    bool rawtty = false;

    log_init ("kzutil");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'a': /* --attach NAME */
                aopt = true;
                key = xstrdup (optarg);
                break;
            case 'c': /* --copy */
                copt = true;
                break;
            case 'k': /* --key NAME */
                key = xstrdup (optarg);
                break;
            case 'r': /* --raw-tty */
                rawtty = true;
                break;
            case 'd': /* --delay-commit */
                kzoutflags |= KZ_FLAGS_DELAYCOMMIT;
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
    if (!aopt && !copt)
        usage ();
    if (copt) {
        if (argc != 2)
            usage ();
    } else {
        if (argc != 0)
            usage ();
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");

    if (aopt) {
        attach (h, key, rawtty, kzoutflags, blocksize);
    } else if (copt) {
        copy (h, argv[0], argv[1], kzoutflags, blocksize);
    }

    flux_close (h);

    free (key);
    log_fini ();
    return 0;
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

static void attach_stdout_ready_cb (kz_t *kz, void *arg)
{
    t_kzutil_ctx_t *ctx = arg;
    char *data;
    int len;

    if ((len = kz_get (kz, &data)) < 0) {
        if (errno != EAGAIN)
            log_err_exit ("kz_get stdout");
    } else if (len > 0) {
        if (write_all (STDOUT_FILENO, data, len) < 0)
            log_err_exit ("write_all stdout");
        free (data);
    }
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

static void attach_stderr_ready_cb (kz_t *kz, void *arg)
{
    t_kzutil_ctx_t *ctx = arg;
    int len;
    char *data;

    if ((len = kz_get (kz, &data)) < 0) {
        if (errno != EAGAIN)
            log_err_exit ("kz_get stderr");
    } else if (len > 0) {
        if (write_all (STDERR_FILENO, data, len) < 0)
            log_err_exit ("write_all stderr");
        free (data);
    }
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

static void attach_stdin_ready_cb (flux_reactor_t *r, flux_watcher_t *w,
                                   int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    t_kzutil_ctx_t *ctx = arg;
    char *buf = xzmalloc (ctx->blocksize);
    int len;

    do  {
        if ((len = read (fd, buf, ctx->blocksize)) < 0) {
            if (errno != EAGAIN)
                log_err_exit ("read stdin");
        } else if (len > 0) {
            if (kz_put (ctx->kz[0], buf, len) < 0)
                log_err_exit ("kz_put");
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (kz_close (ctx->kz[0]) < 0)
            log_err_exit ("kz_close");
    }
    free (buf);
}

static void attach (flux_t *h, const char *key, bool rawtty, int kzoutflags,
                    int blocksize)
{
    t_kzutil_ctx_t *ctx = xzmalloc (sizeof (*ctx));
    char *name;
    int fdin = dup (STDIN_FILENO);
    struct termios saved_tio;
    flux_reactor_t *r = flux_get_reactor (h);
    flux_watcher_t *w = NULL;

    log_msg ("process attached to %s", key);

    ctx->h = h;
    ctx->blocksize = blocksize;

    /* FIXME: need a ~. style escape sequence to terminate stdin
     * in raw mode.
     */
    if (rawtty) {
        if (fd_set_raw (fdin, &saved_tio, true) < 0)
            log_err_exit ("fd_set_raw stdin");
    }
    if (fd_set_nonblocking (fdin, true) < 0)
        log_err_exit ("fd_set_nonblocking stdin");

    if (asprintf (&name, "%s.stdin", key) < 0)
        oom ();
    if (!(ctx->kz[0] = kz_open (h, name, kzoutflags)))
        if (errno == EEXIST)
            log_err ("disabling stdin");
        else
            log_err_exit ("%s", name);
    else {
        if (!(w = flux_fd_watcher_create (r, fdin, FLUX_POLLIN,
                                attach_stdin_ready_cb, ctx)))
            log_err_exit ("flux_fd_watcher_create %s", name);
        flux_watcher_start (w);
    }
    free (name);

    if (asprintf (&name, "%s.stdout", key) < 0)
        oom ();
    if (!(ctx->kz[1] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        log_err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[1], attach_stdout_ready_cb, ctx) < 0)
        log_err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    if (asprintf (&name, "%s.stderr", key) < 0)
        oom ();
    if (!(ctx->kz[2] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        log_err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[2], attach_stderr_ready_cb, ctx) < 0)
        log_err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    /* Reactor terminates when ctx->readers reaches zero, i.e.
     * when EOF is read from remote stdout and stderr.
     * (Note: if they are already at eof, we will have already terminated
     * before the reactor is started, since kvs_watch callbacks make one
     * call to the callback in the context of the caller).
     */
    if (ctx->readers > 0) {
        if (flux_reactor_run (r, 0) < 0)
            log_err_exit ("flux_reactor_run");
    }

    (void)kz_close (ctx->kz[1]);
    (void)kz_close (ctx->kz[2]);

    /* FIXME: tty state needs to be restored on all exit paths.
     */
    if (rawtty) {
        if (fd_set_raw (fdin, &saved_tio, false) < 0)
            log_err_exit ("fd_set_raw stdin");
    }

    flux_watcher_destroy (w);
    free (ctx);
}

static void copy_k2k (flux_t *h, const char *src, const char *dst,
                      int kzoutflags)
{
    kz_t *kzin, *kzout;
    char *json_str;
    bool eof = false;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ | KZ_FLAGS_RAW)))
        log_err_exit ("kz_open %s", src);
    if (!(kzout = kz_open (h, dst, kzoutflags | KZ_FLAGS_RAW)))
        log_err_exit ("kz_open %s", dst);
    while (!eof && (json_str = kz_get_json (kzin))) {
        if (kz_put_json (kzout, json_str) < 0)
            log_err_exit ("kz_put_json %s", dst);
        eof = zio_json_eof (json_str);
        free (json_str);
    }
    if (json_str == NULL)
        log_err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        log_err_exit ("kz_close %s", src);
    if (kz_close (kzout) < 0) 
        log_err_exit ("kz_close %s", dst);
}

static void copy_f2k (flux_t *h, const char *src, const char *dst,
                      int kzoutflags, int blocksize)
{
    int srcfd = STDIN_FILENO;
    kz_t *kzout;
    char *data;
    int len;

    if (strcmp (src, "-") != 0) {
        if ((srcfd = open (src, O_RDONLY)) < 0)
            log_err_exit ("%s", src);
    }
    if (!(kzout = kz_open (h, dst, kzoutflags)))
        log_err_exit ("kz_open %s", dst);
    data = xzmalloc (blocksize);
    while ((len = read (srcfd, data, blocksize)) > 0) {
        if (kz_put (kzout, data, len) < 0)
            log_err_exit ("kz_put %s", dst);
    }
    if (len < 0)
        log_err_exit ("read %s", src);
    free (data);
    if (kz_close (kzout) < 0) 
        log_err_exit ("kz_close %s", dst);
}

static void copy_k2f (flux_t *h, const char *src, const char *dst)
{
    kz_t *kzin;
    int dstfd = STDOUT_FILENO;
    char *data;
    int len;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ)))
        log_err_exit ("kz_open %s", src);
    if (strcmp (dst, "-") != 0) {
        if ((dstfd = creat (dst, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
            log_err_exit ("%s", dst);
    }
    while ((len = kz_get (kzin, &data)) > 0) {
        if (write_all (dstfd, data, len) < 0)
            log_err_exit ("write_all %s", dst);
        free (data);
    }
    if (len < 0)
        log_err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        log_err_exit ("kz_close %s", src);
    if (dstfd != STDOUT_FILENO) {
        if (close (dstfd) < 0)
            log_err_exit ("close %s", dst);
    }
}

static bool isfile (const char *name)
{
    return (!strcmp (name, "-") || strchr (name, '/'));
}

static void copy (flux_t *h, const char *src, const char *dst, int kzoutflags,
                  int blocksize)
{
    if (!isfile (src) && !isfile (dst)) {
        copy_k2k (h, src, dst, kzoutflags);
    } else if (isfile (src) && !isfile (dst)) {
        copy_f2k (h, src, dst, kzoutflags, blocksize);
    } else if (!isfile (src) && isfile (dst)) {
        copy_k2f (h, src, dst);
    } else {
        log_err_exit ("copy src and dst cannot both be file");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
