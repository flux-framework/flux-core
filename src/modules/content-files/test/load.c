#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/libutil/read_all.h"
#include "src/common/libutil/log.h"

#include "src/modules/content-files/filedb.h"

int main (int argc, char **argv)
{
    const char *errstr = NULL;
    void *data;
    size_t size;

    if (argc != 3) {
        fprintf (stderr, "Usage: test_load dbpath key >output\n");
        exit (1);
    }
    if (filedb_get (argv[1], argv[2], &data, &size, &errstr) < 0)
        log_msg_exit ("filedb_get: %s", errstr ? errstr : strerror (errno));

    log_msg ("%zu bytes", size);

    if (write_all (STDOUT_FILENO, data, size) < 0)
        log_err_exit ("writing to stdout");

    free (data);
    exit (0);
}

/*
 * vi:ts=4 sw=4 expandtab
 */

