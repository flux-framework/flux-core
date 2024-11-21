/*
 *  fdwalk.c -- originally from WebRTC linuxfdwalk.c
 *  https://chromium.googlesource.com/external/webrtc/
 *
 *  Original LICENSE and Copyright follow.
 *
 *  Copyright (c) 2011, The WebRTC project authors. All rights reserved.
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google nor the names of its contributors may
 *   be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/* getdents64() based version inspired by glib safe_fdwalk.c:
 *
 * See https://github.com/GNOME/glib/blob/master/glib/gspawn.c
 *
 * File Copyright duplicated here:
 * gspawn.c - Process launching
 *
 *  Copyright 2000 Red Hat, Inc.
 *  g_execvpe implementation based on GNU libc execvp:
 *   Copyright 1991, 92, 95, 96, 97, 98, 99 Free Software Foundation, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#ifdef __linux__
#include <sys/syscall.h> /* syscall and SYS_getdents64 */
struct linux_dirent64
{
  uint64_t       d_ino;    /* 64-bit inode number */
  uint64_t       d_off;    /* 64-bit offset to next structure */
  unsigned short d_reclen; /* Size of this dirent */
  unsigned char  d_type;   /* File type */
  char           d_name[]; /* Filename (null-terminated) */
};
#endif /* __linux__ */

#include "fdwalk.h"

#ifdef __linux__
// Parses a file descriptor number in base 10, requiring the strict format used
// in /proc/*/fd. Returns the value, or -1 if not a valid string.
static int parse_fd(const char *s) {
    if (!*s) {
        // Empty string is invalid.
        return -1;
    }
    int val = 0;
    do {
        if (*s < '0' || *s > '9') {
            // Non-numeric characters anywhere are invalid.
            return -1;
        }
        int digit = *s++ - '0';
        val = val * 10 + digit;
    } while (*s);
    return val;
}
#endif /* __linux__ */

int _fdwalk_portable (void (*func)(void *, int), void *data)
{
    int open_max = sysconf (_SC_OPEN_MAX);
    for (int fd = 0; fd < open_max; fd++)
        func (data, fd);
    return 0;

}

int fdwalk (void (*func)(void *, int), void *data){

    /* On Linux use getdents64 to avoid malloc in opendir() and
     *  still walk only open fds. If not on linux or open() of /proc/self
     *  fails, fall back to iterating over all possible fds.
     */
#ifdef __linux__
    int fd;
    int rc = 0;

    int dir_fd = open ("/proc/self/fd", O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        char buf [4096];
        int pos, n;
        struct linux_dirent64 *de = NULL;

        while ((n = syscall (SYS_getdents64, dir_fd, buf, sizeof(buf))) > 0) {
            for (pos = 0; pos < n; pos += de->d_reclen) {
                de = (struct linux_dirent64 *)(buf + pos);

                fd = parse_fd (de->d_name);
                if (fd > 0 && fd != dir_fd)
                    func (data, fd);
            }
        }

        close (dir_fd);
        return rc;
    }
#endif
    return _fdwalk_portable (func, data);
}

/* vi: ts=4 sw=4 expandtab
 */
