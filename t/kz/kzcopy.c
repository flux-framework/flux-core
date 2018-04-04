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


static void copy (flux_t *h, const char *src, const char *dst, int kzoutflags,
                  int blocksize);

#define OPTIONS "hb:d"
static const struct option longopts[] = {
    {"help",         no_argument,        0, 'h'},
    {"delay-commit", no_argument,        0, 'd'},
    {"blocksize",    required_argument,  0, 'b'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: kzcopy [OPTIONS] from to\n"
"Where OPTIONS are:\n"
"  -b,--blocksize BYTES  set stdin blocksize (default 4096)\n"
"  -d,--delay-commit     flush data to KVS lazily (defer commit until close)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    int blocksize = 4096;
    int kzoutflags = KZ_FLAGS_WRITE;
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

    copy (h, argv[0], argv[1], kzoutflags, blocksize);

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
