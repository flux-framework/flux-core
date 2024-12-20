/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_FILEHASH_H
#define SHELL_FILEHASH_H 1

#include <flux/core.h>

struct file_entry {
    struct filehash *fh;
    char *path;
    int flags;
    int fd;
    bool label;
    int refcount;
};

struct filehash *filehash_create ();
void filehash_destroy (struct filehash *fh);

/*  Open a file at `path` with `flags` if not already open.
 */
struct file_entry *filehash_open (struct filehash *fh,
                                  flux_error_t *errp,
                                  const char *path,
                                  int flags,
                                  bool label);

struct file_entry *filehash_entry_incref (struct file_entry *fp);

/*  Decrement refcount for file entry `fp` in filehash and close `fp`
 *  and free associated memory if refcount == 0.
 */
void file_entry_close (struct file_entry *fp);

/*  Write data with rank label if fp->label && rank >= 0 to file.
 */
int file_entry_write (struct file_entry *fp,
                      const char *label,
                      const char *data,
                      int len);

#endif /* !SHELL_FILEHASH_H */
