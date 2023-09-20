/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <zmq.h>

#include "src/common/libzmqutil/cert.h"
#include "src/common/libutil/log.h"

static struct optparse_option opts[] = {
    { .name = "name", .key = 'n', .has_arg = 1, .arginfo = "NAME",
      .usage = "Set certificate name (default: hostname)", },
    { .name = "meta", .has_arg = 1, .arginfo = "KEYVALS",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Add/update comma-separated key=value metadata", },
    OPTPARSE_TABLE_END,
};

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        return (NULL);
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

/* Mimic zcert_set() behavior of doing nothing (silently) if metadata
 * key already has a value.
 */
static void meta_set (struct cert *cert, const char *key, const char *val)
{
    if (!cert_meta_get (cert, key)
        && cert_meta_set (cert, key, val) < 0)
        log_err_exit ("error setting certificate metadata %s=%s", key, val);
}

static void meta_set_fmt (struct cert *cert,
                          const char *key,
                          const char *fmt,
                          ...)
{
    char buf[1024];
    va_list ap;

    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    meta_set (cert, key, buf);
}

int main (int argc, char *argv[])
{
    const char *usage_msg = "[OPTIONS] [PATH]";
    optparse_t *p;
    int optindex;
    struct cert *cert;
    const char *name = NULL;
    char now[64];
    char hostname[64];
    char *path = NULL;

    log_init ("flux-keygen");
    if (!(p = optparse_create ("flux-keygen"))
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS
        || optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_err_exit ("error setting up option parsing");
    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if (optindex < argc)
        path = argv[optindex++];
    if (optindex < argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!path)
        log_msg ("WARNING: add PATH argument to save generated certificate");

    if (!(cert = cert_create ()))
        log_err_exit ("error creating CURVE certificate");
    if (gethostname (hostname, sizeof (hostname)) < 0)
        log_err_exit ("gethostname");
    if (ctime_iso8601_now (now, sizeof (now)) == NULL)
        log_err_exit ("localtime");

    if ((name = optparse_get_str (p, "name", NULL)))
        meta_set (cert, "name", name);
    if (optparse_hasopt (p, "meta")) {
        const char *arg;

        optparse_getopt_iterator_reset (p, "meta");
        while ((arg = optparse_getopt_next (p, "meta"))) {
            char *key;
            char *val;
            if (!(key = strdup (arg)))
                log_msg_exit ("out of memory");
            if ((val = strchr (key, '=')))
                *val++ = '\0';
            meta_set (cert, key, val ? val : "");
            free (key);
        }
    }
    meta_set (cert, "name", hostname); // used in overlay logging
    meta_set (cert, "keygen.hostname", hostname);
    meta_set (cert, "keygen.time", now);
    meta_set_fmt (cert, "keygen.userid", "%d", getuid ());
    meta_set (cert, "keygen.flux-core-version", FLUX_CORE_VERSION_STRING);
    meta_set_fmt (cert,
                  "keygen.zmq-version",
                  "%d.%d.%d",
                  ZMQ_VERSION_MAJOR,
                  ZMQ_VERSION_MINOR,
                  ZMQ_VERSION_PATCH);

    if (path) {
        int fd;
        FILE *f;
        if ((fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0
            || !(f = fdopen (fd, "w")))
            log_err_exit ("open %s", path);
        if (cert_write (cert, f) < 0)
            log_err_exit ("write %s", path);
        if (fclose (f) < 0)
            log_err_exit ("close %s", path);
    }
    cert_destroy (cert);

    optparse_destroy (p);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
