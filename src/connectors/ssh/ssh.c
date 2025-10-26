/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <unistd.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <libgen.h>
#include <flux/core.h>

#include "src/common/libutil/popen2.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/strstrip.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libyuarel/yuarel.h"
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif
#ifndef HAVE_STRLCAT
#include "src/common/libmissing/strlcat.h"
#endif
#include "src/common/librouter/usock.h"

struct ssh_connector {
    struct usock_client *uclient;
    struct popen2_child *p;
    flux_t *h;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollevents (ctx->uclient);
}

static int op_pollfd (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollfd (ctx->uclient);
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_send (ctx->uclient, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_recv (ctx->uclient, flags);
}

static void op_fini (void *impl)
{
    struct ssh_connector *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        usock_client_destroy (ctx->uclient);
        pclose2 (ctx->p);
        free (ctx);
        errno = saved_errno;
    }
}

static const char *which_dir (const char *prog, char *buf, size_t size)
{
    char *path = getenv ("PATH");
    char *cpy = path ? strdup (path) : NULL;
    char *dir, *saveptr = NULL, *a1 = cpy;
    struct stat sb;
    char *result = NULL;

    if (cpy) {
        while ((dir = strtok_r (a1, ":", &saveptr))) {
            snprintf (buf, size, "%s/%s", dir, prog);
            if (stat (buf, &sb) == 0
                && S_ISREG (sb.st_mode)
                && access (buf, X_OK) == 0) {
                result = dirname (buf);
                break;
            }
            a1 = NULL;
        }
    }
    free (cpy);
    return result;
}

static int make_path (char *path, size_t size, const char *sockpath)
{
    char *sockpath_cpy;
    char buf[1024];
    const char *rundir;
    const char *bindir;
    int rc = -1;

    if (strlcpy (path, "PATH=", size) >= size
        || !(sockpath_cpy = strdup (sockpath)))
        return -1;

    // append rundir/bin
    rundir = dirname (sockpath_cpy);
    if (rundir[0] != '/') {
        if (strlcat (path, "/", size) >= size)
            goto error;
    }
    if (strlcat (path, rundir, size) >= size)
        goto error;
    if (strlcat (path, "/bin", size) >= size)
        goto error;

    // append directory in which flux(1) was found locally
    if ((bindir = which_dir ("flux", buf, sizeof (buf)))) {
        if (strlcat (path, ":", size) >= size)
            goto error;
        if (strlcat (path, bindir, size) >= size)
            goto error;
    }

    // append system bin so libtool wrappers can work if necessary
    if (strlcat (path, ":/bin:/usr/bin", size) >= size)
        goto error;
    rc = 0;
error:
    free (sockpath_cpy);
    return rc;
}

/* uri_path is interpreted as:
 *   [user@]hostname[:port]/unix-path
 * Sets *argvp, *argbuf (caller must free).
 * The last argv[] element is a NULL (required by popen2).
 * Returns 0 on success, -1 on failure with errno set.
 */
int build_ssh_command (const char *uri_path,
                       const char *ssh_cmd,
                       const char *flux_cmd,
                       const char *ld_lib_path,
                       char ***argvp,
                       char **argbuf)
{
    char buf[PATH_MAX + 1];
    struct yuarel yuri;
    char *cpy;
    char *argz = NULL;
    size_t argz_len = 0;
    int argc;
    char **argv;

    if (asprintf (&cpy, "ssh://%s", uri_path) < 0)
        return -1;
    if (yuarel_parse (&yuri, cpy) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (!yuri.path || !yuri.host || yuri.query || yuri.fragment) {
        errno = EINVAL;
        goto error;
    }
    /* ssh */
    if (argz_add (&argz, &argz_len, ssh_cmd) != 0)
        goto nomem;

    /* [-p port] */
    if (yuri.port != 0) {
        (void)snprintf (buf, sizeof (buf), "%d", yuri.port);
        if (argz_add (&argz, &argz_len, "-p") != 0)
            goto nomem;
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }
    /* [user@]hostname */
    if (yuri.username) {
        (void)snprintf (buf, sizeof (buf), "%s@%s", yuri.username, yuri.host);
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }
    else {
        if (argz_add (&argz, &argz_len, yuri.host) != 0)
            goto nomem;
    }

    /* [env] */
    if (ld_lib_path || !flux_cmd) {
        if (argz_add (&argz, &argz_len, "env") != 0)
            goto nomem;
    }
    /* [PATH=remote_path] */
    if (!flux_cmd) {
        if (make_path (buf, sizeof (buf), yuri.path) == 0) {
            if (argz_add (&argz, &argz_len, buf) != 0)
                goto nomem;
        }
        flux_cmd = "flux";
    }
    /* [LD_LIBRARY_PATH=ld_lib_path] */
    if (ld_lib_path) {
        (void)snprintf (buf, sizeof (buf), "LD_LIBRARY_PATH=%s", ld_lib_path);
        if (argz_add (&argz, &argz_len, buf) != 0)
            goto nomem;
    }

    /* flux-relay */
    if (argz_add (&argz, &argz_len, flux_cmd) != 0)
        goto nomem;
    if (argz_add (&argz, &argz_len, "relay") != 0)
        goto nomem;

    /* path */
    (void)snprintf (buf, sizeof (buf), "/%s", yuri.path);
    if (argz_add (&argz, &argz_len, buf) != 0)
        goto nomem;

    /* Convert argz to argv needed by popen2()
     */
    argc = argz_count (argz, argz_len) + 1;
    if (!(argv = calloc (argc, sizeof (argv[0]))))
        goto error;
    argz_extract (argz, argz_len, argv);

    free (cpy);

    *argvp = argv;
    *argbuf = argz;

    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (free, cpy);
    ERRNO_SAFE_WRAP (free, argz);
    return -1;
}

flux_t *connector_init (const char *path, int flags, flux_error_t *errp)
{
    struct ssh_connector *ctx;
    const char *ssh_cmd;
    const char *flux_cmd;
    const char *ld_lib_path;
    char *argbuf = NULL;
    char **argv = NULL;
    int popen_flags = 0;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;

    /* FLUX_SSH may be used to select a different remote shell command
     * from the compiled-in default.  Most rsh variants ought to work.
     */
    if (!(ssh_cmd = getenv ("FLUX_SSH")))
        ssh_cmd = PATH_SSH;
    /* FLUX_SSH_RCMD may be used to force a specific path to the flux
     * command front end.
     */
    flux_cmd = getenv ("FLUX_SSH_RCMD");

    /* ssh and rsh do not forward environment variables, thus LD_LIBRARY_PATH
     * is not guaranteed to be set on the remote node where the flux command is
     * run.  If the flux command is linked against libraries that can only be
     * found when LD_LIBRARY_PATH is set, then the flux command will fail to
     * run over ssh.  Grab the client-side LD_LIBRARY_PATH so that we can
     * manually forward it. See flux-core issue #3457 for more details.
     */
    ld_lib_path = getenv ("LD_LIBRARY_PATH");

    /* Construct argv for ssh command from uri "path" (non-scheme part)
     * and flux and ssh command paths.
     */
    if (build_ssh_command (path,
                           ssh_cmd,
                           flux_cmd,
                           ld_lib_path,
                           &argv,
                           &argbuf) < 0)
        goto error;

    /* Start the ssh command
     */
    if (errp)
        popen_flags = POPEN2_CAPTURE_STDERR;
    if (!(ctx->p = popen2 (ssh_cmd, argv, popen_flags))) {
        /* If popen fails because ssh cannot be found, flux_open()
         * will just fail with errno = ENOENT, which is not all that helpful.
         * Emit a hint on stderr even though this is perhaps not ideal.
         * (if errp is non-NULL then use that instead)
         */
        if (errp) {
            errprintf (errp,
                       "ssh-connector: %s: %s\n"
                       "Hint: set FLUX_SSH in environment to override",
                       ssh_cmd,
                       strerror (errno));
            goto error;
        }
        fprintf (stderr, "ssh-connector: %s: %s\n", ssh_cmd, strerror (errno));
        fprintf (stderr, "Hint: set FLUX_SSH in environment to override\n");
        goto error;
    }
    /* The ssh command is the "client" here, tunneling through flux-relay
     * to a remote local:// connector.  The "auth handshake" is performed
     * between this client and flux-relay.  The byte returned is always zero,
     * but performing this handshake forces some errors to be handled here
     * inside flux_open() rather than in some less obvious context later.
     */
    if (!(ctx->uclient = usock_client_create (popen2_get_fd (ctx->p)))) {
        char *data = NULL;

        /* Set stderr fd to nonblocking to avoid a hang in read_all() when
         * the client uconn connection has failed, but the remote command
         * has not exited. This can occur, for example, if there is stdout
         * emitted from shell rc files like .bashrc or .cshrc.
         */
        fd_set_nonblocking (popen2_get_stderr_fd (ctx->p));
        if (read_all (popen2_get_stderr_fd (ctx->p), (void **) &data) > 0)
            errprintf (errp, "%s", strstrip (data));
        free (data);
        goto error;
    }
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    free (argbuf);
    free (argv);
    return ctx->h;
error:
    if (ctx) {
        if (ctx->h)
            flux_handle_destroy (ctx->h); /* calls op_fini */
        else
            op_fini (ctx);
    }
    ERRNO_SAFE_WRAP (free, argbuf);
    ERRNO_SAFE_WRAP (free, argv);
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
