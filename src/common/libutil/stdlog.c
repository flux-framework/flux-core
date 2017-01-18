
/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <stdarg.h>

#include "stdlog.h"

/* Header: PRI VERSION SP TIMESTAMP SP HOSTNAME
           SP APP-NAME SP PROCID SP MSGID
 * Body: SP STRUCTURED-DATA SP MESSAGE
 */

static int next_int (char **p, int *ip)
{
    char *endptr;
    int i = strtoul (*p, &endptr, 10);
    if (*p == endptr)
        return -1;
    *p = endptr + 1;
    *ip = i;
    return 0;
}

static int next_str (char **p, char **result)
{
    char *this = *p;
    char *next = *p;

    while (*next != '\0' && *next != ' ')
        next++;
    if (next == *p)
        return -1;
    if (*next != '\0')
        *next++ = '\0';
    *p = next;
    *result = this;
    return 0;
}

static int next_structured_data (const char *buf, int len, int *offp,
                                 const char **sp, int *slenp)
{
    int off = *offp;
    int this = *offp;
    int level = 0;

    while (off < len) {
        if (buf[off] == '[')
            level++;
        else if (buf[off] == ']')
            level--;
        else if (buf[off] == ' ' && level == 0)
            break;
        off++;
    }
    if (off == len)
        return -1;
    *offp = off + 1;
    if (sp)
        *sp = &buf[this];
    if (slenp)
        *slenp = off - this;
    return 0;
}

int stdlog_decode (const char *buf, int len, struct stdlog_header *hdr,
                   const char **sdp, int *sdlenp,
                   const char **msgp, int *msglenp)
{
    int hdr_len = STDLOG_MAX_HEADER;
    char *p = &hdr->buf[0];
    const char *sd;
    int off, sdlen;

    if (hdr_len > len)
        hdr_len = len;
    strncpy (hdr->buf, buf, hdr_len);
    hdr->buf[hdr_len] = '\0';

    if (*p++ != '<')
        return -1;
    if (next_int (&p, &hdr->pri) < 0)
        return -1;
    if (next_int (&p, &hdr->version) < 0)
        return -1;
    if (next_str (&p, &hdr->timestamp) < 0)
        return -1;
    if (next_str (&p, &hdr->hostname) < 0)
        return -1;
    if (next_str (&p, &hdr->appname) < 0)
        return -1;
    if (next_str (&p, &hdr->procid) < 0)
        return -1;
    if (next_str (&p, &hdr->msgid) < 0)
        return -1;
    /* Switch to original unterminated buffer
     * (structured data and message are not copied).
     * FIXME: handle charset and BOM in message?
     */
    off = p - &hdr->buf[0];
    if (next_structured_data (buf, len, &off, &sd, &sdlen) < 0)
        return -1;
    if (sdp)
        *sdp = sd;
    if (sdlenp)
        *sdlenp = sdlen;
    if (msgp)
        *msgp = &buf[off];
    if (msglenp)
        *msglenp = len - off;
    return 0;
}


int stdlog_vencodef (char *buf, int len, struct stdlog_header *hdr,
                     const char *sd, const char *fmt, va_list ap)
{
    int m, n;
    int rc; // includes any overflow

    m = snprintf (buf, len, "<%d>%d %.*s %.*s %.*s %.*s %.*s %s ",
                  hdr->pri, hdr->version,
                  STDLOG_MAX_TIMESTAMP, hdr->timestamp,
                  STDLOG_MAX_HOSTNAME, hdr->hostname,
                  STDLOG_MAX_APPNAME, hdr->appname,
                  STDLOG_MAX_PROCID, hdr->procid,
                  STDLOG_MAX_MSGID, hdr->msgid,
                  sd);
    rc = m;
    if (m > len)
        m = len;

    n = vsnprintf (buf + m, len - m, fmt, ap);
    rc += n;
    if (n > len - m)
        n = len - m;
    return rc;
}

int stdlog_encodef (char *buf, int len, struct stdlog_header *hdr,
                    const char *sd, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = stdlog_vencodef (buf, len, hdr, sd, fmt, ap);
    va_end (ap);
    return rc;
}

int stdlog_encode (char *buf, int len, struct stdlog_header *hdr,
                   const char *sd, const char *msg)
{
    return stdlog_encodef (buf, len, hdr, sd, "%s", msg);
}

void stdlog_init (struct stdlog_header *hdr)
{
    memset (hdr->buf, 0, STDLOG_MAX_HEADER + 1);
    hdr->pri = ((LOG_INFO<<3) | LOG_USER);
    hdr->version = 1;
    hdr->timestamp = STDLOG_NILVALUE;
    hdr->hostname = STDLOG_NILVALUE;
    hdr->appname = STDLOG_NILVALUE;
    hdr->procid = STDLOG_NILVALUE;
    hdr->msgid = STDLOG_NILVALUE;
}


struct matchtab {
    char *s;
    int n;
};

static struct matchtab severity_tab[] = {
    { "emerg",  LOG_EMERG },
    { "alert",  LOG_ALERT },
    { "crit",   LOG_CRIT },
    { "err",    LOG_ERR },
    { "warning", LOG_WARNING },
    { "notice", LOG_NOTICE },
    { "info",   LOG_INFO },
    { "debug",  LOG_DEBUG },
    { NULL,     0},
};

const char *
stdlog_severity_to_string (int n)
{
    int i;

    for (i = 0; severity_tab[i].s != NULL; i++)
        if (severity_tab[i].n == n)
            return severity_tab[i].s;
    return STDLOG_NILVALUE;
}

int
stdlog_string_to_severity (const char *s)
{
    int i;

    for (i = 0; severity_tab[i].s != NULL; i++)
        if (!strcasecmp (severity_tab[i].s, s))
            return severity_tab[i].n;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
