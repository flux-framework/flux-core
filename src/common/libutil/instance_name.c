/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <string.h>

#include "ccan/str/str.h"

#include "instance_name.h"

/* Emit the sanitized form of 's' to 'buf' (if non-NULL), skipping the first
 * 'skip' output characters, and return the number of characters the full
 * sanitized form would have.
 */
static size_t sanitize (const char *s, char *buf, size_t size, size_t skip)
{
    size_t n = 0;   // characters of sanitized output seen so far
    size_t o = 0;   // characters written to buf
    size_t i = 0;   // position in s

    /* Advance over the input once, classifying each character.  The F58
     * prefix is the two UTF-8 bytes 0xc6 0x92 and is consumed as a unit, so
     * the stride is decided up front rather than inside the classification.
     */
    while (s[i] != '\0') {
        unsigned char c = s[i];
        bool f58_prefix = (c == 0xc6 && (unsigned char)s[i + 1] == 0x92);
        char out;

        i += f58_prefix ? 2 : 1;

        if (f58_prefix)
            out = 'f';              // emit the f58plain equivalent
        else if (c == '/') {
            if (n == 0)
                continue;           // suppress a leading separator
            out = '-';
        }
        else if ((c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '_'
            || c == '.'
            || c == '-')
            out = c;
        else
            continue;               // anything else is dropped
        if (n++ >= skip && buf && o < size - 1)
            buf[o++] = out;
    }
    if (buf)
        buf[o] = '\0';
    return n;
}

void instance_name_sanitize (const char *s, char *buf, size_t size)
{
    size_t len = sanitize (s, NULL, 0, 0);

    /* Drop leading characters when the name does not fit.  A name is built
     * by appending, so its tail is the part that identifies this instance
     * rather than its ancestors, and is the part worth keeping.
     */
    sanitize (s, buf, size, len > size - 1 ? len - (size - 1) : 0);
}

bool instance_name_valid (const char *s)
{
    char buf[INSTANCE_NAME_MAX + 1];

    if (!s || strlen (s) == 0 || strlen (s) > INSTANCE_NAME_MAX)
        return false;
    instance_name_sanitize (s, buf, sizeof (buf));
    return streq (buf, s);
}

// vi:ts=4 sw=4 expandtab
