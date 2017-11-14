#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

/* Current kvs_watch() API doesn't accept a rank, so we cheat and
 * build the minimal kvs.watch request message from scratch here.
 * Read the first response and pretend we're done.  If this key actually
 * changes then we are screwed here because we will have recycled the
 * matchtags but more responses will be coming.
 */
void send_watch_requests (flux_t *h, const char *key)
{
    int flags = KVS_WATCH_FIRST;
    flux_mrpc_t *r;

    if (!(r = flux_mrpcf (h, "kvs.watch", "all", 0, "{s:s s:s s:i s:n}",
                                                    "key", key,
                                                    "namespace", KVS_PRIMARY_NAMESPACE,
                                                    "flags", flags,
                                                    "val")))
        log_err_exit ("flux_mrpc kvs.watch");
    do {
        if (flux_mrpc_get (r, NULL) < 0)
            log_err_exit ("kvs.watch");
    } while (flux_mrpc_next (r) == 0);
    flux_mrpc_destroy (r);
}

/* Sum #watchers over all ranks.
 */
int count_watchers (flux_t *h)
{
    int n, count = 0;
    flux_mrpc_t *r;

    if (!(r = flux_mrpc (h, "kvs.stats.get", NULL, "all", 0)))
        log_err_exit ("flux_mrpc kvs.stats.get");
    do {
        if (flux_mrpc_getf (r, "{s:i}", "#watchers", &n) < 0)
            log_err_exit ("kvs.stats.get #watchers");
        count += n;
    } while (flux_mrpc_next (r) == 0);
    flux_mrpc_destroy (r);
    return count;
}

int main (int argc, char **argv)
{
    flux_t *h;
    int w0, w1, w2;

    /* Install watchers on every rank, then disconnect.
     * The number of watchers should return to the original count.
     */
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    w0 = count_watchers (h);
    send_watch_requests (h, "nonexist");
    w1 = count_watchers (h) - w0;
    log_msg ("test watchers: %d", w1);
    flux_close (h);
    log_msg ("disconnected");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    w2 = count_watchers (h) - w0;
    log_msg ("test watchers: %d", w2);
    if (w2 != 0)
        log_err_exit ("Test failure, watchers were not removed on disconnect");

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
