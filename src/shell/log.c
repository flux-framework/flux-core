/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell log facility
 *
 * Implements logging facility for shell components and plugins
 * (see shell.h for public interfaces)
 *
 * By default all messages at logger.level or below are logged
 * to stderr. If the shell plugin stack has been initialized
 * logging messages are additionally sent to any "shell.log"
 * callbacks, allowing other logging implementations to be loaded
 * in the shell at runtime.
 *
 */
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "ccan/str/str.h"

#include "info.h"
#include "internal.h"
#include "log.h"

static struct {
    FILE *fp;
    char *prog;
    int level;
    int fp_level;
    int rank;
    unsigned int active:1;
    unsigned int exception_logged:1;
    flux_shell_t *shell;
} logger;

const char *levelstr[] = {
    "FATAL", "FATAL", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
};

/*  Format a log message in standard form to stream fp */
static void log_fprintf (FILE *fp,
                         int level,
                         const char *component,
                         const char *file,
                         int line,
                         const char *msg)
{
    const char *prefix = levelstr [level];

    fprintf (logger.fp, "%s: ", logger.prog);
    if (prefix)
        fprintf (logger.fp, "%5s: ", prefix);
    if (component)
        fprintf (logger.fp, "%s: ", component);

    /* Log file:line information if level >= FLUX_SHELL_DEBUG */
    if (logger.level > FLUX_SHELL_NOTICE && file && line > 0)
        fprintf (logger.fp, "%s:%d: ", file, line);

    fprintf (logger.fp, "%s\n", msg);
}

static flux_plugin_arg_t *log_msg_args (int level,
                                        int rank,
                                        const char *component,
                                        const char *file,
                                        int line,
                                        const char *msg)
{
    int rc = -1;
    int flags = FLUX_PLUGIN_ARG_IN;
    flux_plugin_arg_t *args = flux_plugin_arg_create ();

    if (!args)
        return NULL;
    if (flux_plugin_arg_pack (args, flags,
                              "{ s:i s:i s:s }",
                              "rank", rank,
                              "level", level,
                              "message", msg) < 0)
        goto out;
    if (component && flux_plugin_arg_pack (args, flags,
                                           "{ s:s }",
                                           "component", component) < 0)
        goto out;
    if (file && flux_plugin_arg_pack (args, flags,
                                      "{ s:s s:i }",
                                      "file", file,
                                      "line", line) < 0)
        goto out;
    rc = 0;
out:
    if (rc < 0) {
        flux_plugin_arg_destroy (args);
        return NULL;
    }
    return args;
}


static int log_event (int level,
                      int rank,
                      const char *component,
                      const char *file,
                      int line,
                      const char *msg)
{
    int rc = 0;
    flux_plugin_arg_t *args;

    /*  If plugin stack is not initialized yet, or log level is at or
     *   below our threshold, log the message to stderr
     */
    if (!logger.shell->plugstack || level <= logger.fp_level)
        log_fprintf (stderr, level, component, file, line, msg);

    /*  If plugin stack is initialized, and we're not actively processing
     *   another log message then format log message args and call all
     *   "shell.log" callbacks.
     */
    if (logger.shell->plugstack && !logger.active) {
        logger.active = 1;
        if (!(args = log_msg_args (level, rank, component, file, line, msg)))
            return -1;
        rc = flux_shell_plugstack_call (logger.shell, "shell.log", args);
        flux_plugin_arg_destroy (args);
        logger.active = 0;
    }
    return rc;
}

static void send_logmsg (const char *buf,
                         int level,
                         const char *component,
                         const char *file,
                         int line)
{
    if (logger.rank < 0 && logger.shell->info)
        logger.rank = logger.shell->info->shell_rank;
    if (log_event (level, logger.rank, component, file, line, buf) < 0)
        fprintf (stderr, "%s: log failure: %s\n", logger.prog, buf);
}

static int errorcat (int errnum, int start, char *buf, size_t len)
{
    char *p;
    int n, size;

    if (errnum == 0)
        return 0;

    size = len - start;
    p = buf + start;
    if ((n = snprintf (p, size, ": %s", strerror (errnum))) >= size)
        return -1;
    return n;
}

/*  Format message, appending a '+' if the buffer isn't large enough.
 *  If errnum > 0, then append result of strerror (errnum).
 */
static void msgfmt (char *buf,
                    size_t len,
                    int errnum,
                    const char *fmt,
                    va_list ap)
{
    int rc;
    if ((rc = vsnprintf (buf, len, fmt, ap)) >= len
        || errorcat (errnum, rc, buf, len) < 0)
        buf[len - 2] = '+';
    /*  Clean up trailing newline, pointless here
     */
    else if (rc > 0 && buf[rc - 1] == '\n')
        buf[rc - 1] = '\0';
}

void flux_shell_log (const char *component,
                     int level,
                     const char *file,
                     int line,
                     const char *fmt, ...)
{
    char buf [4096];
    va_list ap;
    va_start (ap, fmt);
    msgfmt (buf, sizeof (buf), 0, fmt, ap);
    send_logmsg (buf, level, component, file, line);
    va_end (ap);
}

/* llog compatible wrapper for flux_shell_log
 */
void shell_llog (void *arg,
                 const char *file,
                 int line,
                 const char *func,
                 const char *subsys,
                 int level,
                 const char *fmt,
                 va_list ap)
{
    char buf [4096];
    int buflen = sizeof (buf);
    int n = vsnprintf (buf, buflen, fmt, ap);
    if (n >= buflen) {
        buf[buflen-1] = '\0';
        buf[buflen-2] = '+';
    }
    flux_shell_log (subsys, level, file, line, "%s", buf);
}

int flux_shell_err (const char *component,
                    const char *file,
                    int line,
                    int errnum,
                    const char *fmt, ...)
{
    char buf [4096];
    va_list ap;
    va_start (ap, fmt);
    msgfmt (buf, sizeof (buf), errnum, fmt, ap);
    send_logmsg (buf, FLUX_SHELL_ERROR, component, file, line);
    va_end (ap);
    errno = errnum;
    return -1;
}

void flux_shell_raise (const char *type,
                      int severity,
                      const char *fmt, ...)
{
    flux_shell_t *shell = logger.shell;
    flux_future_t *f;
    char buf [4096];
    va_list ap;

    if (!shell || !shell->h || !shell->info || logger.exception_logged)
        return;

    va_start (ap, fmt);
    msgfmt (buf, sizeof (buf), 0, fmt, ap);
    va_end (ap);

    if (!(f = flux_job_raise (shell->h,
                              shell->info->jobid,
                              "exec",
                              0,
                              buf))
       || flux_future_get (f, NULL) < 0) {
        fprintf (stderr,
                 "flux-shell: failed to raise job exception: %s\n",
                 flux_future_error_string (f));
    }
    else
        shell_log_set_exception_logged ();
    flux_future_destroy (f);
}

void flux_shell_fatal (const char *component,
                       const char *file,
                       int line,
                       int errnum,
                       int exit_code,
                       const char *fmt, ...)
{
    flux_shell_t *shell = logger.shell;
    char buf [4096];
    va_list ap;

    va_start (ap, fmt);
    msgfmt (buf, sizeof (buf), errnum, fmt, ap);
    send_logmsg (buf, FLUX_SHELL_FATAL, component, file, line);
    va_end (ap);

    /*  Attempt to kill any running tasks
     */
    flux_shell_killall (shell, SIGKILL);

    if (shell)
        flux_shell_raise ("exec", 0, "%s", buf);

    exit (exit_code);
}

void shell_log_set_exception_logged (void)
{
    logger.exception_logged = 1;
}

static int log_setlevel (flux_shell_t *shell, const char *dest, int level)
{
    int rc = -1;
    flux_plugin_arg_t *args;

    if (!shell || !dest) {
        errno = EINVAL;
        return -1;
    }
    if (!shell->plugstack) {
        errno = EAGAIN;
        return -1;
    }
    if (!(args = flux_plugin_arg_create ()))
        return -1;
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_IN,
                              "{ s:s s:i }",
                              "dest", dest,
                              "level", level) < 0)
        goto out;
    rc = flux_shell_plugstack_call (shell, "shell.log-setlevel", args);
out:
    flux_plugin_arg_destroy (args);
    return rc;
}

int flux_shell_log_setlevel (int level, const char *dest)
{
    if (level < FLUX_SHELL_QUIET || level > FLUX_SHELL_TRACE) {
        errno = EINVAL;
        return -1;
    }

    /*  Always set internal dispatch level.
     */
    if (level > logger.level)
        logger.level = level;

    if (dest == NULL)
        return 0;

    /*  If dest is set, then attempt to notify any loaded loggers of
     *   the severity level change.
     */
    if (dest != NULL) {
        if (streq (dest, "stderr"))
            logger.fp_level = level;
        else
            return log_setlevel (logger.shell, dest, level);
    }
    return 0;
}

int shell_log_init (flux_shell_t *shell, const char *progname)
{
    logger.shell = shell;
    logger.level = FLUX_SHELL_NOTICE;
    logger.fp_level = FLUX_SHELL_NOTICE;
    logger.active = 0;
    logger.exception_logged = 0;
    logger.fp = stderr;
    logger.rank = -1;
    if (progname && !(logger.prog = strdup (progname)))
        return -1;
    return 0;
}

int shell_log_reinit (flux_shell_t *shell)
{
   if (shell->verbose > 2) {
        shell_warn ("Ignoring shell verbosity > 2");
        shell->verbose = 2;
    }
    if (flux_shell_log_setlevel (FLUX_SHELL_NOTICE + shell->verbose, "any") < 0)
        shell_die (1, "failed to set log level");
    return 0;
}

void shell_log_fini (void)
{
    logger.shell = NULL;
    free (logger.prog);
    fclose (logger.fp);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
