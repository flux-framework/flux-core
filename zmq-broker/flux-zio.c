/* flux-zio.c - copy stdio in/out of the KVS */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>
#include <sys/wait.h>
#include <termios.h>

#include "cmb.h"
#include "util.h"
#include "log.h"
#include "zio.h"
#include "forkzio.h"
#include "kz.h"

typedef struct {
    flux_t h;
    void *zs;
    kz_t kz[3];
    int readers;
    int blocksize;
} ctx_t;

static void copy (flux_t h, const char *src, const char *dst, bool trunc,
                  int blocksize);
static void attach (flux_t h, const char *key, int flags, bool trunc,
                   int blocksize);
static void run (flux_t h, const char *key, int ac, char **av, int flags);

#define OPTIONS "hra:cpk:dfb:"
static const struct option longopts[] = {
    {"help",         no_argument,        0, 'h'},
    {"run",          no_argument,        0, 'r'},
    {"attach",       required_argument,  0, 'a'},
    {"copy",         no_argument,        0, 'c'},
    {"key",          required_argument,  0, 'k'},
    {"pty",          no_argument,        0, 'p'},
    {"debug",        no_argument,        0, 'd'},
    {"force",        no_argument,        0, 'f'},
    {"blocksize",    required_argument,  0, 'b'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-zio [OPTIONS] --run CMD ...\n"
"       flux-zio [OPTIONS] --attach NAME\n"
"       flux-zio [OPTIONS] --copy from to\n"
"Where OPTIONS are:\n"
"  -k,--key NAME         set KVS target for zio streams\n"
"  -p,--pty              run/attach using a pty\n"
"  -f,--force            truncate stdin on write [copy,attach]\n"
"  -b,--blocksize BYTES  set stdin blocksize (default 4096) [copy,attach]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    bool copt = false;
    bool aopt = false;
    bool ropt = false;
    bool fopt = false;
    char *key = NULL;
    int flags = 0;
    int blocksize = 4096;
    flux_t h;

    log_init ("flux-zio");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'a': /* --attach NAME */
                aopt = true;
                key = xstrdup (optarg);
                break;
            case 'c': /* --copy */
                copt = true;
                break;
            case 'r': /* --run */
                ropt = true;
                break;
            case 'k': /* --key NAME */
                key = xstrdup (optarg);
                break;
            case 'p': /* --pty */
                flags |= FORKZIO_FLAG_PTY;
                break;
            case 'd': /* --debug */
                flags |= FORKZIO_FLAG_DEBUG;
                break;
            case 'f': /* --force */
                fopt = true;
                break;
            case 'b': /* --blocksize bytes */
                blocksize = strtoul (optarg, NULL, 10);
                break;
            default:  /* --help|? */
                usage ();
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (!ropt && !aopt && !copt)
        usage ();
    if (ropt) {
        if (argc == 0)
            usage ();
    } else if (copt) {
        if (argc != 2)
            usage ();
    } else {
        if (argc != 0)
            usage ();
    }

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if ((aopt || ropt) && !key) {
        if (asprintf (&key, "zio.%d.%d", flux_rank (h), (int)getpid ()) < 0)
            oom ();
    }

    if (aopt) {
        attach (h, key, flags, fopt, blocksize);
    } else if (ropt) {
        run (h, key, argc, argv, flags);
    } else if (copt) {
        copy (h, argv[0], argv[1], fopt, blocksize);
    }

    flux_handle_destroy (&h);

    free (key);
    log_fini ();
    return 0;
}

static int run_send_kz (kz_t *kzp, char *data, int len)
{
    int rc = -1;

    if (!*kzp) {
        errno = EPROTO;
        goto done;
    }
    if (len == 0) {
        if (kz_close (*kzp) < 0)
            goto done;
        *kzp = NULL;
    } else {
        if (kz_put (*kzp, data, len) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

static json_object *run_recv_zs (void *zs)
{
    zmsg_t *zmsg = zmsg_recv (zs);
    json_object *o = NULL;
    char *buf = NULL;

    if (!zmsg || !(buf = zmsg_popstr (zmsg)) || strlen (buf) == 0)
        goto done;
    if (!(o = json_tokener_parse (buf)))
        goto done;
done:
    if (buf)
        free (buf);
    if (zmsg)
        zmsg_destroy (&zmsg);
    return o;
}

static int run_zs_ready_cb (flux_t h, void *zs, short revents, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o;
    char *stream = NULL;
    bool eof;
    char *data = NULL;
    int len = 0;
    int rc = -1;

    if (!(o = run_recv_zs (zs))) {
        flux_reactor_stop (h);
        rc = 0;
        goto done;
    }
    len = zio_json_decode (o, &data, &eof, &stream);
    if (len < 0 || (len > 0 && eof)) {
        errno = EPROTO;
        goto done;
    }
    if (!strcmp (stream, "stdout")) {
        if (run_send_kz (&ctx->kz[1], data, len) < 0)
            goto done;        
    } else if (!strcmp (stream, "stderr")) {
        if (run_send_kz (&ctx->kz[2], data, len) < 0)
            goto done;        
    } else {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    if (data)
        free (data);
    if (stream)   
        free (stream); 
    if (o)
        json_object_put (o);
    return rc;
}

static int run_send_zs (void *zs, json_object *o)
{
    zmsg_t *zmsg;
    const char *s;
    int rc = -1;

    if (!(zmsg = zmsg_new ()))
        oom ();
    s = json_object_to_json_string (o);
    if (zmsg_addstr (zmsg, s) < 0)
        goto done;
    if (zmsg_send (&zmsg, zs) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

static void run_stdin_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    int len;
    char *data;
    json_object *o;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stdin");
        } else if (len > 0) {
            if (!(o = zio_json_encode (data, len, false, "stdin")))
                err_exit ("zio_json_encode");
            if (run_send_zs (ctx->zs, o) < 0)
                err_exit ("run_send_zs");
            free (data);
            json_object_put (o);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (!(o = zio_json_encode (NULL, 0, true, "stdin")))
            err_exit ("zio_json_encode");
        if (run_send_zs (ctx->zs, o) < 0)
            err_exit ("run_send_zs");
        json_object_put (o);
    }
}

static void run (flux_t h, const char *key, int ac, char **av, int flags)
{
    zctx_t *zctx = zctx_new ();
    forkzio_t fz;
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    char *name;

    ctx->h = h;

    msg ("process attached to %s", key);

    if (!(fz = forkzio_open (zctx, ac, av, flags)))
        err_exit ("forkzio_open");
    ctx->zs = forkzio_get_zsocket (fz);
    if (flux_zshandler_add (ctx->h, ctx->zs, ZMQ_POLLIN,
                            run_zs_ready_cb, ctx) < 0)
        err_exit ("flux_zshandler_add"); 

    if (asprintf (&name, "%s.stdin", key) < 0)
        oom ();
    ctx->kz[0] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK);
    if (!ctx->kz[0])
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[0], run_stdin_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);

    if (asprintf (&name, "%s.stdout", key) < 0)
        oom ();
    ctx->kz[1] = kz_open (h, name, KZ_FLAGS_WRITE);
    if (!ctx->kz[1])
        err_exit ("kz_open %s", name);
    free (name);

    if (asprintf (&name, "%s.stderr", key) < 0)
        oom ();
    ctx->kz[2] = kz_open (h, name, KZ_FLAGS_WRITE);
    if (!ctx->kz[2])
        err_exit ("kz_open %s", name);
    free (name);

    if (flux_reactor_start (ctx->h) < 0)
        err_exit ("flux_reactor_start");
    forkzio_close (fz);

    (void)kz_close (ctx->kz[0]);

    zmq_term (zctx);
    free (ctx);
}

static int fd_set_raw (int fd, struct termios *tio_save, bool goraw)
{
    struct termios tio;

    if (goraw) { /* save */
        if (tcgetattr (STDIN_FILENO, &tio) < 0)
            return -1;
        *tio_save = tio;
        cfmakeraw (&tio);
        if (tcsetattr (STDIN_FILENO, TCSANOW, &tio) < 0)
            return -1;
    } else { /* restore */
        if (tcsetattr (STDIN_FILENO, TCSANOW, tio_save) < 0)
            return -1;
    }
    return 0;
}

static int fd_set_nonblocking (int fd, bool nonblock)
{
    int fval;

    assert (fd >= 0);

    if ((fval = fcntl (fd, F_GETFL, 0)) < 0)
        return (-1);
    if (nonblock)
        fval |= O_NONBLOCK;
    else
        fval &= ~O_NONBLOCK;
    if (fcntl (fd, F_SETFL, fval) < 0)
        return (-1);
    return (0);
}

static int write_all (int fd, char *buf, int len)
{
    int n, count = 0;

    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
}

static void attach_stdout_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    char *data;
    int len;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stdout");
        } else if (len > 0) {
            if (write_all (STDOUT_FILENO, data, len) < 0)
                err_exit ("write_all stdout");
            free (data);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (ctx->h);
    }
}

static void attach_stderr_ready_cb (kz_t kz, void *arg)
{
    ctx_t *ctx = arg;
    int len;
    char *data;

    do {
        if ((len = kz_get (kz, &data)) < 0) {
            if (errno != EAGAIN)
                err_exit ("kz_get stderr");
        } else if (len > 0) {
            if (write_all (STDERR_FILENO, data, len) < 0)
                err_exit ("write_all stderr");
            free (data);
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (--ctx->readers == 0)
            flux_reactor_stop (ctx->h);
    }
}

static int attach_stdin_ready_cb (flux_t h, int fd, short revents, void *arg)
{
    ctx_t *ctx = arg;
    char *buf = xzmalloc (ctx->blocksize);
    int len;

    do  {
        if ((len = read (fd, buf, ctx->blocksize)) < 0) {
            if (errno != EAGAIN)
                err_exit ("read stdin");
        } else if (len > 0) {
            if (kz_put (ctx->kz[0], buf, len) < 0)
                err_exit ("kz_put");
        }
    } while (len > 0);
    if (len == 0) { /* EOF */
        if (kz_close (ctx->kz[0]) < 0)
            err_exit ("kz_close");
    }
    free (buf);
    return 0;
}

static void attach (flux_t h, const char *key, int flags, bool trunc,
                    int blocksize)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    char *name;
    int fdin = dup (STDIN_FILENO);
    struct termios saved_tio;
    int kzoutflags = KZ_FLAGS_WRITE | (trunc ? KZ_FLAGS_TRUNC : 0);

    msg ("process attached to %s", key);
    
    ctx->h = h;
    ctx->blocksize = blocksize;

    /* FIXME: need a ~. style escape sequence to terminate stdin
     * in raw mode.
     */
    if ((flags & FORKZIO_FLAG_PTY)) {
        if (fd_set_raw (fdin, &saved_tio, true) < 0)
            err_exit ("fd_set_raw stdin");
    }
    if (fd_set_nonblocking (fdin, true) < 0)
        err_exit ("fd_set_nonblocking stdin");

    if (asprintf (&name, "%s.stdin", key) < 0)
        oom ();
    if (!(ctx->kz[0] = kz_open (h, name, kzoutflags)))
        if (errno == EEXIST)
            err ("disabling stdin");
        else
            err_exit ("%s", name);
    else {
        if (flux_fdhandler_add (h, fdin, ZMQ_POLLIN | ZMQ_POLLERR,
                                attach_stdin_ready_cb, ctx) < 0)
            err_exit ("flux_fdhandler_add %s", name);
    }
    free (name);

    if (asprintf (&name, "%s.stdout", key) < 0)
        oom ();
    if (!(ctx->kz[1] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[1], attach_stdout_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    if (asprintf (&name, "%s.stderr", key) < 0)
        oom ();
    if (!(ctx->kz[2] = kz_open (h, name, KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK)))
        err_exit ("kz_open %s", name);
    if (kz_set_ready_cb (ctx->kz[2], attach_stderr_ready_cb, ctx) < 0)
        err_exit ("kz_set_ready_cb %s", name);
    free (name);
    ctx->readers++;

    /* Reactor terminates when ctx->readers reaches zero, i.e.
     * when EOF is read from remote stdout and stderr.
     * (Note: if they are already at eof, we will have already terminated
     * before the reactor is started, since kvs_watch callbacks make one
     * call to the callback in the context of the caller).
     */
    if (ctx->readers > 0) {
        if (flux_reactor_start (ctx->h) < 0)
            err_exit ("flux_reactor_start");
    }

    (void)kz_close (ctx->kz[1]);
    (void)kz_close (ctx->kz[2]);

    /* FIXME: tty state needs to be restored on all exit paths.
     */
    if ((flags & FORKZIO_FLAG_PTY)) {
        if (fd_set_raw (fdin, &saved_tio, false) < 0)
            err_exit ("fd_set_raw stdin");
    }

    free (ctx);
}

static void copy_k2k (flux_t h, const char *src, const char *dst, bool trunc)
{
    int kzoutflags = KZ_FLAGS_WRITE | (trunc ? KZ_FLAGS_TRUNC : 0);
    kz_t kzin, kzout;
    char *data;
    int len;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ)))
        err_exit ("kz_open %s", src);
    if (!(kzout = kz_open (h, dst, kzoutflags)))
        err_exit ("kz_open %s", dst);
    while ((len = kz_get (kzin, &data)) > 0) {
        if (kz_put (kzout, data, len) < 0)
            err_exit ("kz_put %s", dst);
        free (data);
    }
    if (len < 0)
        err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        err_exit ("kz_close %s", src);
    if (kz_close (kzout) < 0) 
        err_exit ("kz_close %s", dst);
}

static void copy_f2k (flux_t h, const char *src, const char *dst, bool trunc,
                      int blocksize)
{
    int srcfd = STDIN_FILENO;
    int kzoutflags = KZ_FLAGS_WRITE | (trunc ? KZ_FLAGS_TRUNC : 0);
    kz_t kzout;
    char *data;
    int len;

    if (strcmp (src, "-") != 0) {
        if ((srcfd = open (src, O_RDONLY)) < 0)
            err_exit ("%s", src);
    }
    if (!(kzout = kz_open (h, dst, kzoutflags)))
        err_exit ("kz_open %s", dst);
    data = xzmalloc (blocksize);
    while ((len = read (srcfd, data, blocksize)) > 0) {
        if (kz_put (kzout, data, len) < 0)
            err_exit ("kz_put %s", dst);
    }
    if (len < 0)
        err_exit ("read %s", src);
    free (data);
    if (kz_close (kzout) < 0) 
        err_exit ("kz_close %s", dst);
}

static void copy_k2f (flux_t h, const char *src, const char *dst)
{
    kz_t kzin;
    int dstfd = STDOUT_FILENO;
    char *data;
    int len;

    if (!(kzin = kz_open (h, src, KZ_FLAGS_READ)))
        err_exit ("kz_open %s", src);
    if (strcmp (dst, "-") != 0) {
        if ((dstfd = creat (dst, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
            err_exit ("%s", dst);
    }
    while ((len = kz_get (kzin, &data)) > 0) {
        if (write_all (dstfd, data, len) < 0)
            err_exit ("write_all %s", dst);
        free (data);
    }
    if (len < 0)
        err_exit ("kz_get %s", src);
    if (kz_close (kzin) < 0) 
        err_exit ("kz_close %s", src);
    if (dstfd != STDOUT_FILENO) {
        if (close (dstfd) < 0)
            err_exit ("close %s", dst);
    }
}

static bool isfile (const char *name)
{
    return (!strcmp (name, "-") || strchr (name, '/'));
}

static void copy (flux_t h, const char *src, const char *dst, bool trunc,
                  int blocksize)
{
    if (!isfile (src) && !isfile (dst)) {
        copy_k2k (h, src, dst, trunc);
    } else if (isfile (src) && !isfile (dst)) {
        copy_f2k (h, src, dst, trunc, blocksize);
    } else if (!isfile (src) && isfile (dst)) {
        copy_k2f (h, src, dst);
    } else {
        err_exit ("copy src and dst cannot both be file");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
