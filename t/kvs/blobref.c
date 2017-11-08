#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <jansson.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/blobref.h"
#include "src/common/libutil/readall.h"

int main (int argc, char *argv[])
{
    uint8_t *data;
    int size;
    char *hashtype;
    blobref_t blobref;

    if (argc != 2) {
        fprintf (stderr, "Usage: cat file | blobref hashtype\n");
        exit (1);
    }
    hashtype = argv[1];

    if ((size = read_all (STDIN_FILENO, &data)) < 0)
        log_err_exit ("read");

    if (blobref_hash (hashtype, data, size, blobref) < 0)
        log_err_exit ("blobref_hash");
    printf ("%s\n", blobref);
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
