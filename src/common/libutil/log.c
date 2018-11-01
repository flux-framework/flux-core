/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <ctype.h>
#include <libgen.h>
#include <syslog.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <zmq.h>

#include "log.h"

extern char *__progname;
static char *prog = NULL;

void
log_init (char *p)
{
    if (!p)
        prog = __progname;
    else
        prog = basename (p);
}

void
log_fini (void)
{
}

static void
_verr (int errnum, const char *fmt, va_list ap)
{
    char *msg = NULL;
    char buf[128];
    const char *s;

    /* zeromq-4.2.1 reports EHOSTUNREACH as "Host unreachable",
     * but "No route to host" is canonical on Linux and we have some
     * tests that depend on it, so remap here.
     */
    if (errnum == EHOSTUNREACH)
        s = "No route to host";
    else
        s = zmq_strerror (errnum);

    if (!prog)
        log_init (NULL);
    if (vasprintf (&msg, fmt, ap) < 0) {
        (void)vsnprintf (buf, sizeof (buf), fmt, ap);
        msg = buf;
    }
    fprintf (stderr, "%s: %s: %s\n", prog, msg, s);
    if (msg != buf)
        free (msg);
}

static void
_vlog (const char *fmt, va_list ap)
{
    char *msg = NULL;
    char buf[128];

    if (!prog)
        log_init (NULL);
    if (vasprintf (&msg, fmt, ap) < 0) {
        (void)vsnprintf (buf, sizeof (buf), fmt, ap);
        msg = buf;
    }
    fprintf (stderr, "%s: %s\n", prog, msg);
    if (msg != buf)
        free (msg);
}

/* Log message and errno string, then exit.
 */
void
log_err_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errno string.
 */
void
log_err (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
}


/* Log message and errnum string, then exit.
 */
void
log_errn_exit (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errnum string.
 */
void
log_errn (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
}

/* Log message, then exit.
 */
void
log_msg_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _vlog (fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message.
 */
void
log_msg (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _vlog (fmt, ap);
    va_end (ap);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
