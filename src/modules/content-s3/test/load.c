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

#include "src/modules/content-s3/s3.h"

int main (int argc, char **argv)
{
    const char *errstr = NULL;
    void *data = NULL;
    size_t size;


    struct s3_config *cfg;

    if (!(cfg = calloc (1, sizeof (*cfg))))
        fprintf(stderr, "calloc error");
    
    cfg->retries = 5;
    cfg->bucket = getenv("S3_BUCKET");
    cfg->access_key = getenv("S3_ACCESS_KEY_ID");
    cfg->secret_key = getenv("S3_SECRET_ACCESS_KEY");
    cfg->hostname = getenv("S3_HOSTNAME");

    if (s3_init (cfg, &errstr) < 0) {
        fprintf(stderr, "S3 init error\n%s\n", errstr);
    }

    if (s3_bucket_create (cfg, &errstr) < 0) {
        fprintf(stderr, "S3 create bucket error\n%s\n", errstr);
    }

    if (argc != 2) {
        fprintf (stderr, "Usage: test_load key >output\n");
        exit (1);
    }
    if (s3_get (cfg, argv[1], &data, &size, &errstr) < 0)
        log_msg_exit ("s3_get: %s", errstr ? errstr : strerror (errno));

    log_msg ("%zu bytes", size);

    if (write_all (STDOUT_FILENO, data, size) < 0)
        log_err_exit ("writing to stdout");

    free (data);
    exit (0);
}

/*
 * vi:ts=4 sw=4 expandtab
 */
