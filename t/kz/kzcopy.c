/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include "src/common/libzio/zio.h"
#include "src/common/libkz/kz.h"

struct copy_context {
    flux_reactor_t *r;
    int fd;
    kz_t *kz;
};


static void copy (flux_t *h, const char *src, const char *dst,
                  int kzoutflags, int kzinflags, int blocksize);

#define OPTIONS "hb:dnN"
static const struct option longopts[] = {
    {"help",         no_argument,        0, 'h'},
    {"delay-commit", no_argument,        0, 'd'},
    {"blocksize",    required_argument,  0, 'b'},
    {"non-blocking", no_argument,        0, 'n'},
    {"no-follow",    no_argument,        0, 'N'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: kzcopy [OPTIONS] from to\n"
"Where OPTIONS are:\n"
"  -b,--blocksize BYTES  set stdin blocksize (default 4096)\n"
"  -d,--delay-commit     flush data to KVS lazily (defer commit until close)\n"
"  -n,--non-blocking     use KZ_FLAGS_NONBLOCK and callbacks to copy\n"
"  -N,--no-follow        use KZ_FLAGS_NOFOLLOW to copy from KVS\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    int blocksize = 4096;
    int kzoutflags = KZ_FLAGS_WRITE;
    int kzinflags = KZ_FLAGS_READ;
    flux_t *h;
    uint32_t rank;

    log_init ("kzcopy");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'd': /* --delay-commit */
                kzoutflags |= KZ_FLAGS_DELAYCOMMIT;
                break;
            case 'b': /* --blocksize bytes */
                blocksize = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --non-blocking */
                kzinflags |= KZ_FLAGS_NONBLOCK;
                break;
            case 'N': /* --no-follow */
                kzinflags |= KZ_FLAGS_NOFOLLOW;
                break;
            default:  /* --help|? */
                usage ();
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc != 2)
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");

    copy (h, argv[0], argv[1], kzoutflags, kzinflags, blocksize);

    flux_close (h);

    log_fini ();
    return 0;
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

static void copy_k2k_cb (kz_t *kz, void *arg)
{
    struct copy_context *ctx = arg;
    char *json_str;

    json_str = kz_get_json (kz);
    if (!json_str)
        log_err_exit ("kz_get_json");
    if (kz_put_json (ctx->kz, json_str) < 0)
        log_err_exit ("kz_put_json");
    if (zio_json_eof (json_str)) // EOF
        flux_reactor_stop (ctx->r);
    free (json_str);
}

static void copy_k2k (flux_t *h, const char *src, const char *dst,
                      int kzinflags, int kzoutflags)
{
    kz_t *kzin, *kzout;

    /* Open
     */
    if (!(kzin = kz_open (h, src, kzinflags | KZ_FLAGS_RAW)))
        log_err_exit ("kz_open %s", src);
    if (!(kzout = kz_open (h, dst, kzoutflags | KZ_FLAGS_RAW)))
        log_err_exit ("kz_open %s", dst);
    /* Nonblocking mode - use callbacks
     */
    if ((kzinflags & KZ_FLAGS_NONBLOCK)) {
        struct copy_context ctx;
        ctx.kz = kzout;
        ctx.r = flux_get_reactor (h);
        if (kz_set_ready_cb (kzin, copy_k2k_cb, &ctx) < 0)
            log_err_exit ("kz_set_ready_cb");
        if (flux_reactor_run (ctx.r, 0) <  0)
            log_err_exit ("flux_reactor_run");
    }
    /* Blocking mode - copy with simple get/put loop
     */
    else {
        char *json_str;
        bool eof;
        do {
            if (!(json_str = kz_get_json (kzin)))
                log_err_exit ("kz_get_json %s", src);
            if (kz_put_json (kzout, json_str) < 0)
                log_err_exit ("kz_put_json %s", dst);
            eof = zio_json_eof (json_str);
            free (json_str);
        } while (!eof);
        if (json_str == NULL)
            log_err_exit ("kz_get %s", src);
    }
    /* Close
     */
    if (kz_close (kzin) < 0)
        log_err_exit ("kz_close %s", src);
    if (kz_close (kzout) < 0)
        log_err_exit ("kz_close %s", dst);
}

static void copy_f2k_noeof (flux_t *h, const char *src, const char *dst,
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
    if (!(kzout = kz_open (h, dst, kzoutflags | KZ_FLAGS_RAW)))
        log_err_exit ("kz_open %s", dst);
    data = xzmalloc (blocksize);
    while ((len = read (srcfd, data, blocksize)) > 0) {
        char *json_str = zio_json_encode (data, len, false);
        if (kz_put_json (kzout, json_str) < 0)
            log_err_exit ("kz_put_json %s", dst);
        free (json_str);
    }
    if (len < 0)
        log_err_exit ("read %s", src);
    free (data);
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

static void copy_k2f_cb (kz_t *kz, void *arg)
{
    struct copy_context *ctx = arg;
    void *data;
    int len;

    len = kz_get (kz, (char **)&data);
    if (len < 0)
        log_err_exit ("kz_get");
    else if (len == 0) // EOF
        flux_reactor_stop (ctx->r);
    else { // len > 0
        if (write_all (ctx->fd, data, len) < 0)
            log_err_exit ("write output file");
        free (data);
    }
}

static void copy_k2f (flux_t *h, const char *src, const char *dst,
                      int kzinflags)
{
    kz_t *kzin;
    int dstfd = STDOUT_FILENO;

    /* Open
     */
    if (!(kzin = kz_open (h, src, kzinflags)))
        log_err_exit ("kz_open %s", src);
    if (strcmp (dst, "-") != 0) {
        if ((dstfd = creat (dst, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
            log_err_exit ("%s", dst);
    }
    /* Nonblocking mode - use callbacks
     */
    if ((kzinflags & KZ_FLAGS_NONBLOCK)) {
        struct copy_context ctx;
        ctx.fd = dstfd;
        ctx.r = flux_get_reactor (h);
        if (kz_set_ready_cb (kzin, copy_k2f_cb, &ctx) < 0)
            log_err_exit ("kz_set_ready_cb");
        if (flux_reactor_run (ctx.r, 0) <  0)
            log_err_exit ("flux_reactor_run");
    }
    /* Blocking mode - copy with simple get/put loop
     */
    else {
        char *data;
        int len;
        while ((len = kz_get (kzin, &data)) > 0) {
            if (write_all (dstfd, data, len) < 0)
                log_err_exit ("write_all %s", dst);
            free (data);
        }
        if (len < 0)
            log_err_exit ("kz_get %s", src);
    }
    /* Close
     */
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

static void copy (flux_t *h, const char *src, const char *dst,
                  int kzoutflags, int kzinflags, int blocksize)
{
    if (!isfile (src) && !isfile (dst)) {
        copy_k2k (h, src, dst, kzinflags, kzoutflags);
    } else if (isfile (src) && !isfile (dst)) {
        if (kzinflags & KZ_FLAGS_NOFOLLOW)
            copy_f2k_noeof (h, src, dst, kzoutflags, blocksize);
        else
            copy_f2k (h, src, dst, kzoutflags, blocksize);
    } else if (!isfile (src) && isfile (dst)) {
        copy_k2f (h, src, dst, kzinflags);
    } else {
        log_err_exit ("copy src and dst cannot both be file");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
