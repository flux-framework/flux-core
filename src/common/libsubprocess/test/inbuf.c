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
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "inbuf.h"

/* check some line buffering corner cases:
 * - Residual buffer (no \n) becomes a reactor event when EOF flushes it
 * - Line longer than buflen is returned in bufsize - 1 length chunks
 * - Two lines in one write are two reactor events
 * - One line in two writes is one reactor event
 */

const char *send_phrases[] = {
    "Narf!",
    "Zort!\nPoit!\n",
    "Egad, Brain!\n",
    "Gonk!\n",
    "Come, Pinky-o. We must catch the space shuttle back to our"
        " home planet of Acme and prepare for the next millennium.\n",
    "Fjord!\n",
    "Gat!\n",
    "Zounds!",
    NULL,
};

struct expect {
    int rc;
    const char *s;
};

struct expect recv_phrases_linebuf[] = {
    { 11,   "Narf!Zort!\n" },
    { 6,    "Poit!\n" },
    { 13,   "Egad, Brain!\n" },
    { 6,    "Gonk!\n" },
    { 39,   "Come, Pinky-o. We must catch the space " }, /* truncated */
    { 39,   "shuttle back to our home planet of Acme" },
    { 38,   " and prepare for the next millennium.\n" },
    { 7,    "Fjord!\n" },
    { 5,    "Gat!\n" },
    { 7,    "Zounds!" }, /* flushed out by eof */
    { 0,    NULL }, /* EOF */
};

struct expect recv_phrases_regbuf[] = {
    { 5,    "Narf!" },
    { 12,   "Zort!\nPoit!\n" },
    { 13,   "Egad, Brain!\n" },
    { 6,    "Gonk!\n" },
    { 40,   "Come, Pinky-o. We must catch the space s" },
    { 40,   "huttle back to our home planet of Acme a" },
    { 36,   "nd prepare for the next millennium.\n" },
    { 7,    "Fjord!\n" },
    { 5,    "Gat!\n" },
    { 7,    "Zounds!" },
    { 0,    NULL }, /* EOF */
};

const int bufsize = 40;

struct ctx {
    int fds[2];
    int read_count;
    int write_count;
};

const char *str_esc (const char *s, int len)
{
    static char buf[1024];
    char *p = buf;
    memset (buf, 0, sizeof (buf));
    while (len > 0 && s && *s && p - buf < sizeof (buf)) {
        if (*s == '\n') {
            *p++ = '\\' ;
            *p++ = 'n';
        } else
            *p++ = *s;
        s++;
        len--;
    }
    return buf;
}

void linebuf_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct ctx *ctx = arg;
    struct expect *exp = &recv_phrases_linebuf[ctx->read_count];
    char buf[bufsize];
    int rc = flux_inbuf_watcher_read (w, buf, sizeof (buf));

    diag ("%s: %d:'%s'", __FUNCTION__, rc,
          rc > 0 ? str_esc (buf, rc) : rc == 0 ? "(EOF)" : strerror (errno));

    ok (rc == exp->rc,
        "%s: read expected return value", __FUNCTION__);
    if (rc > 0)
        ok (rc < bufsize && buf[rc] == '\0' && !strncmp (buf, exp->s, exp->rc),
            "%s: read expected content", __FUNCTION__);
    if (rc == 0) {
        ok (close (ctx->fds[0]) >= 0,
            "%s: EOF: closed read end of socketpair", __FUNCTION__);
        flux_watcher_stop (w);
    }
    ctx->read_count++;
}

void regbuf_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct ctx *ctx = arg;
    struct expect *exp = &recv_phrases_regbuf[ctx->read_count];
    char buf[bufsize];
    int rc = flux_inbuf_watcher_read (w, buf, sizeof (buf));

    diag ("%s: %d:'%s'", __FUNCTION__, rc,
          rc > 0 ? str_esc (buf, rc) : rc == 0 ? "(EOF)" : strerror (errno));

    ok (rc == exp->rc,
        "%s: read expected return value", __FUNCTION__);
    if (rc > 0)
        ok (!strncmp (buf, exp->s, exp->rc),
            "%s: read expected content", __FUNCTION__);
    if (rc == 0) {
        ok (close (ctx->fds[0]) >= 0,
            "%s: EOF: closed read end of socketpair", __FUNCTION__);
        flux_watcher_stop (w);
    }
    ctx->read_count++;
}

void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct ctx *ctx = arg;
    const char *phrase = send_phrases[ctx->write_count];

    if (phrase) {
        int len = strlen (phrase);
        int rc = write (ctx->fds[1], phrase, len);

        ok (rc == len,
            "%s: write '%s'", __FUNCTION__, str_esc (phrase, len));
        if (rc >= 0 && rc < len)
            diag ("%s: short write", __FUNCTION__);
        else if (rc < 0)
            diag ("%s: %s", __FUNCTION__, strerror (errno));
        if (rc != len)
            flux_reactor_stop_error (r);
        ctx->write_count++;
    } else {
        ok (close (ctx->fds[1]) == 0,
            "%s: closed write end of socketpair", __FUNCTION__);
        flux_watcher_stop (w);
    }
}

void test_linebuf (flux_reactor_t *r)
{
    struct ctx ctx;
    int flags = INBUF_LINE_BUFFERED;
    flux_watcher_t *inbuf_w;
    flux_watcher_t *timer_w;

    memset (&ctx, 0, sizeof (ctx));

    ok (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ctx.fds) == 0,
        "obtained socketpair");
    ok ((inbuf_w = flux_inbuf_watcher_create (r, ctx.fds[0], bufsize,
                                              flags, linebuf_cb, &ctx)) != NULL,
        "flux_inbuf_watcher_create worked");
    flux_watcher_start (inbuf_w);

    ok ((timer_w = flux_timer_watcher_create (r, 0.1, 0.1,
                                              timer_cb, &ctx)) != NULL,
        "flux_timer_watcher_create worked");
    flux_watcher_start (timer_w);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor returned normally");

    flux_watcher_destroy (inbuf_w);
    flux_watcher_destroy (timer_w);
}

void test_regbuf (flux_reactor_t *r)
{
    struct ctx ctx;
    int flags = 0;
    flux_watcher_t *inbuf_w;
    flux_watcher_t *timer_w;

    memset (&ctx, 0, sizeof (ctx));

    ok (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, ctx.fds) == 0,
        "obtained socketpair");
    ok ((inbuf_w = flux_inbuf_watcher_create (r, ctx.fds[0], bufsize,
                                              flags, regbuf_cb, &ctx)) != NULL,
        "flux_inbuf_watcher_create worked");
    flux_watcher_start (inbuf_w);

    ok ((timer_w = flux_timer_watcher_create (r, 0.1, 0.1,
                                              timer_cb, &ctx)) != NULL,
        "flux_timer_watcher_create worked");
    flux_watcher_start (timer_w);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor returned normally");

    flux_watcher_destroy (inbuf_w);
    flux_watcher_destroy (timer_w);
}

int main (int argc, char **argv)
{
    struct ctx ctx;
    flux_reactor_t *r;

    plan (NO_PLAN);

    memset (&ctx, 0, sizeof (ctx));

    ok ((r = flux_reactor_create (0)) != NULL,
        "flux reactor created");

    test_linebuf (r);

    test_regbuf (r);

    flux_reactor_destroy (r);

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
