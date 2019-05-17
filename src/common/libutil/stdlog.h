/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_STDLOG_H
#    define _UTIL_STDLOG_H

#    include <stdarg.h>

/* RFC 5424 syslog wire format */

#    define STDLOG_MAX_PRI 5
#    define STDLOG_MAX_VER 3
#    define STDLOG_MAX_TIMESTAMP 32
#    define STDLOG_MAX_HOSTNAME 255
#    define STDLOG_MAX_APPNAME 48
#    define STDLOG_MAX_PROCID 128
#    define STDLOG_MAX_MSGID 32
#    define STDLOG_MAX_HEADER                                           \
        (5 + STDLOG_MAX_PRI + STDLOG_MAX_VER + STDLOG_MAX_TIMESTAMP     \
         + STDLOG_MAX_HOSTNAME + STDLOG_MAX_APPNAME + STDLOG_MAX_PROCID \
         + STDLOG_MAX_MSGID)

#    define STDLOG_NILVALUE "-"

#    define STDLOG_SEVERITY(pri) ((pri) >> 3)
#    define STDLOG_FACILITY(pri) ((pri)&7)
#    define STDLOG_PRI(sev, fac) (((sev) << 3) | ((fac)&7))

struct stdlog_header {
    char buf[STDLOG_MAX_HEADER + 1];
    int pri;
    int version;
    char *timestamp;
    char *hostname;
    char *appname;
    char *procid;
    char *msgid;
};

int stdlog_decode (const char *buf,
                   int len,
                   struct stdlog_header *hdr,
                   const char **sd,
                   int *sdlen,
                   const char **msg,
                   int *msglen);

int stdlog_encode (char *buf,
                   int len,
                   struct stdlog_header *hdr,
                   const char *sd,
                   const char *msg);

int stdlog_vencodef (char *buf,
                     int len,
                     struct stdlog_header *hdr,
                     const char *sd,
                     const char *fmt,
                     va_list ap);

int stdlog_encodef (char *buf,
                    int len,
                    struct stdlog_header *hdr,
                    const char *sd,
                    const char *fmt,
                    ...);

/* If encoded stdlog message in buf, *len contains chars from 'sep'
 * (in the message part), truncate the original message and return
 * buffer containing the rest, with NULL terminator.
 * Returns NULL if no 'sep' chars or on alloc failure.
 * If non-NULL, caller must free returned value.
 */
char *stdlog_split_message (const char *buf, int *len, const char *sep);

void stdlog_init (struct stdlog_header *hdr);

const char *stdlog_severity_to_string (int level);
int stdlog_string_to_severity (const char *s);

#endif /* !_UTIL_STDLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
