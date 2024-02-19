/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job submit */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <flux/core.h>
#include <flux/optparse.h>

#if HAVE_FLUX_SECURITY
#include <flux/security/sign.h>
#endif

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/strstrip.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"

struct optparse_option submit_opts[] =  {
    { .name = "urgency", .key = 'u', .has_arg = 1, .arginfo = "N",
      .usage = "Set job urgency (0-31), hold=0, default=16, expedite=31",
    },
    { .name = "flags", .key = 'f', .has_arg = 1,
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Set submit comma-separated flags (e.g. debug, waitable)",
    },
#if HAVE_FLUX_SECURITY
    { .name = "security-config", .key = 'c', .has_arg = 1, .arginfo = "pattern",
      .usage = "Use non-default security config glob",
    },
    { .name = "sign-type", .key = 's', .has_arg = 1, .arginfo = "TYPE",
      .usage = "Use non-default mechanism type to sign J",
    },
#endif
    OPTPARSE_TABLE_END
};

/* Read entire file 'name' ("-" for stdin).  Exit program on error.
 */
static size_t read_jobspec (const char *name, void **bufp)
{
    int fd;
    ssize_t size;
    void *buf;

    if (streq (name, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (name, O_RDONLY)) < 0)
            log_err_exit ("%s", name);
    }
    if ((size = read_all (fd, &buf)) < 0)
        log_err_exit ("%s", name);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    *bufp = buf;
    return size;
}


static void print_jobid (flux_jobid_t id)
{
    printf ("%s\n", idf58 (id));
}

int cmd_submit (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
#if HAVE_FLUX_SECURITY
    flux_security_t *sec = NULL;
    const char *sec_config;
    const char *sign_type = NULL;
#endif
    int flags = 0;
    void *jobspec;
    int jobspecsz;
    const char *J = NULL;
    int urgency;
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    flux_jobid_t id;
    const char *input = "-";

    if (optindex != argc - 1 && optindex != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < argc)
        input = argv[optindex++];
    if (optparse_hasopt (p, "flags")) {
        const char *name;
        while ((name = optparse_getopt_next (p, "flags"))) {
            if (streq (name, "debug"))
                flags |= FLUX_JOB_DEBUG;
            else if (streq (name, "waitable"))
                flags |= FLUX_JOB_WAITABLE;
            else if (streq (name, "signed"))
                flags |= FLUX_JOB_PRE_SIGNED;
            else if (streq (name, "novalidate"))
                flags |= FLUX_JOB_NOVALIDATE;
            else
                log_msg_exit ("unknown flag: %s", name);
        }
    }
#if HAVE_FLUX_SECURITY
    /* If any non-default security options are specified, create security
     * context so jobspec can be pre-signed before submission.
     */
    if (optparse_hasopt (p, "security-config")
        || optparse_hasopt (p, "sign-type")) {
        if (flags & FLUX_JOB_PRE_SIGNED)
            log_msg ("Ignoring security config with --flags=pre-signed");
        else {
            sec_config = optparse_get_str (p, "security-config", NULL);
            if (!(sec = flux_security_create (0)))
                log_err_exit ("security");
            if (flux_security_configure (sec, sec_config) < 0)
                log_err_exit ("security config %s",
                              flux_security_last_error (sec));
            sign_type = optparse_get_str (p, "sign-type", NULL);
        }
    }
#endif
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if ((jobspecsz = read_jobspec (input, &jobspec)) == 0)
        log_msg_exit ("required jobspec is empty");

    /* If jobspec was pre-signed, then assign J to to jobspec after
     * stripping surrounding whitespace.
     */
    if (flags & FLUX_JOB_PRE_SIGNED)
        J = strstrip (jobspec);

    urgency = optparse_get_int (p, "urgency", FLUX_JOB_URGENCY_DEFAULT);

#if HAVE_FLUX_SECURITY
    if (sec) {
        if (!(J = flux_sign_wrap (sec, jobspec, jobspecsz, sign_type, 0)))
            log_err_exit ("flux_sign_wrap: %s",
                          flux_security_last_error (sec));
        flags |= FLUX_JOB_PRE_SIGNED;
    }
#endif
    if (!(f = flux_job_submit (h, J ? J : jobspec, urgency, flags)))
        log_err_exit ("flux_job_submit");
    if (flux_job_submit_get_id (f, &id) < 0) {
        log_msg_exit ("%s", future_strerror (f, errno));
    }
    print_jobid (id);
    flux_future_destroy (f);
#if HAVE_FLUX_SECURITY
    flux_security_destroy (sec); // invalidates J
#endif
    flux_close (h);
    free (jobspec);
    return 0;
}


/* vi: ts=4 sw=4 expandtab
 */
