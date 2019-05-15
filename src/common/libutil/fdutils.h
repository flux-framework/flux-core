/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_FDUTILS_H
#define _UTIL_FDUTILS_H 1

/*
 *  fdutils: convenient functions for file descriptors
 */

/*  Clear O_NONBLOCK flag from file descriptor fd.
 *  Returns 0 on Success, -1 on Failure with errno set.
 */
int fd_set_blocking (int fd);

/*  Set O_NONBLOCK flag for the file descriptor fd.
 *  Returns original flags on Success, -1 on Failure with errno set.
 */
int fd_set_nonblocking (int fd);

/*  Set O_CLOEXEC file descriptor flag on file descriptor fd.
 *  Returns original flags on Success, -1 on Failure with errno set.
 */
int fd_set_cloexec (int fd);

/* Unset O_CLOEXEC file descriptor flag on file descriptor fd.
 *  Returns original flags on Success, -1 on Failure with errno set.
 */
int fd_unset_cloexec (int fd);

/*  Return current file status flags on file descriptor fd */
int fd_get_flags (int fd);

/*  Set file status flags on file descriptor fd */
int fd_set_flags (int fd, int flags);

#endif /* !_UTIL_FDUTILS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
