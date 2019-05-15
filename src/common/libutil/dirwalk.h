/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_UTIL_DIRWALK_H
#define HAVE_UTIL_DIRWALK_H

#include <czmq.h> /* zlist_t */

typedef struct dirwalk dirwalk_t;

enum {
    DIRWALK_DEPTH = 1 << 0,    /* Traverse in depth-first order             */
    DIRWALK_REALPATH = 1 << 1, /* Resolve all paths with realpath(3)        */
    DIRWALK_FIND_DIR = 1 << 2, /* Do not skip directories in dirwalk_find() */
};

/*
 *  Dirwalk visitor function.
 *  dirwalk_t handle can be used with various accessors below
 *   to return current path, dirfd, stat buffer, etc.
 *  Function can signal error or request to stop traversal with
 *   dirwalk_stop (d, errnum);
 */
typedef int (*dirwalk_filter_f) (dirwalk_t *d, void *arg);

/*
 *  One-shot directory tree walk.
 *
 *  Starting at directory `path` apply `fn` visitor function at each
 *   path, passing user context `arg` to each invocation of the filter.
 *
 *  Returns the number of entries successfully visited, or -1 on
 *   error, e.g. fatal error in traversal or dirwalk_stop() with nonzero
 *   errnum.
 *
 *  Return value from dirwalk_filter_f is ignored, use dirwalk_stop() to
 *   halt traversal.
 */
int dirwalk (const char *path, int flags, dirwalk_filter_f fn, void *arg);

/*
 *  Stop in-progress traversal immediately. If errnum != 0, then dirwalk
 *   traversal will return -1 with errno set to errnum.
 */
void dirwalk_stop (dirwalk_t *d, int errnum);

/*  Return current path visited during a dirwalk. May be a relative
 *   path unless DIRWALK_REALPATH flag was used.
 */
const char *dirwalk_path (dirwalk_t *d);

/*  Return the current file name (as in dirent.d_name) during a dirwalk.
 */
const char *dirwalk_name (dirwalk_t *d);

/*  Return a pointer to the struct stat structure for the current file
 *   being visited during a dirwalk.
 */
const struct stat *dirwalk_stat (dirwalk_t *d);

/*  Return fd for current directory in dirwalk.
 */
int dirwalk_dirfd (dirwalk_t *d);

/*  Return 1 if current path is a directory, 0 if not.
 */
int dirwalk_isdir (dirwalk_t *d);

/*
 *  Search a (possibly colon-separated) path `path` with flags `flags`
 *   and accumulate filenames matching `pattern` returning zlist of results.
 *   Stops traversal after `count` matches are found if count > 0.
 *
 *  If `fn` is non-NULL then function is called for each match and results
 *   are only accumulated if function returns > 0.
 *
 *  zlist must be freed by caller (results are set to autofree).
 */
zlist_t *dirwalk_find (const char *path,
                       int flags,
                       const char *pattern,
                       int count,
                       dirwalk_filter_f fn,
                       void *arg);

#endif /* !HAVE_UTIL_DIRWALK_H */
