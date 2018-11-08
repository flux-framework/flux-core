
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <flux/core.h>

static int total_bytes = 0;

static void die (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    exit (1);
}

static void write_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int errnum;
    if (revents & FLUX_POLLERR)
        die ("got POLLERR on stdout. Aborting\n");

    if (flux_buffer_write_watcher_is_closed (w, &errnum)) {
        if (errnum)
            fprintf (stderr, "error: close: %s\n", strerror (errnum));
        flux_watcher_stop (w);
    }
}

static void read_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    const void *data;
    int len, n = 0;
    flux_watcher_t *writer = arg;
    flux_buffer_t *wfb = NULL;
    flux_buffer_t *rfb = NULL;;

    if (!(wfb = flux_buffer_write_watcher_get_buffer (writer))
        || !(rfb = flux_buffer_read_watcher_get_buffer (w)))
        die ("failed to get read/write buffers from watchers!\n");

    if (!(data = flux_buffer_peek (rfb, -1, &len)))
        die ("flux_buffer_peek: %s\n", strerror (errno));

    if ((len > 0) && ((n = flux_buffer_write (wfb, data, len)) < 0))
        die ("flux_buffer_write: %s\n", strerror (errno));
    else if (len == 0) {
        /* Propagate EOF to writer, stop reader */
        flux_buffer_write_watcher_close (writer);
        flux_watcher_stop (w);
    }

    /* Drop data in read buffer that was successfully written to writer */
    flux_buffer_drop (rfb, n);
    total_bytes += n;
}

static int stdin_fdflags;
static int stdout_fdflags;

static void restore_fdflags (void)
{
    (void)fcntl (STDIN_FILENO, F_SETFL, stdin_fdflags);
    (void)fcntl (STDOUT_FILENO, F_SETFL, stdout_fdflags);
}

int main (int argc, char *argv[])
{
    int rc;
    flux_watcher_t *rw, *ww;
    flux_reactor_t *r;

    if ((stdin_fdflags = fcntl (STDIN_FILENO, F_GETFL)) < 0
            || fcntl (STDIN_FILENO, F_SETFL, stdin_fdflags|O_NONBLOCK) < 0
            || (stdout_fdflags = fcntl (STDOUT_FILENO, F_GETFL)) < 0
            || fcntl (STDOUT_FILENO, F_SETFL, stdout_fdflags|O_NONBLOCK) < 0)
        die ("fcntl");
    if (atexit (restore_fdflags) != 0)
        die ("atexit");

    if (!(r = flux_reactor_create (0)))
        die ("flux_reactor_create failed\n");

    ww = flux_buffer_write_watcher_create (r, STDOUT_FILENO, 4096,
                                           write_cb, 0, NULL);
    rw = flux_buffer_read_watcher_create (r, STDIN_FILENO, 4096,
                                          read_cb, 0, (void *) ww);
    if (!rw || !ww)
        die ("flux buffer watcher create failed\n");

    flux_watcher_start (rw);
    flux_watcher_start (ww);

    if ((rc = flux_reactor_run (r, 0)) < 0)
        die ("flux_reactor_run() returned nonzero\n");

    fprintf (stderr, "debug: %d bytes transferred.\n", total_bytes);

    flux_watcher_destroy (rw);
    flux_watcher_destroy (ww);
    flux_reactor_destroy (r);

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

