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
        fprintf (stderr, "Usage: test_store dbpath key <input\n");
        exit (1);
    }
    size = read_all (STDIN_FILENO, &data);
    if (size < 0)
        log_err_exit ("error reading stdin");

    if (filedb_put (argv[1], argv[2], data, size, &errstr) < 0)
        log_msg_exit ("filedb_put : %s", errstr ? errstr : "failed");

    free (data);

    exit (0);
}

/*
 * vi:ts=4 sw=4 expandtab
 */

