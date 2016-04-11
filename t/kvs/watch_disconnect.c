#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

/* Current kvs_watch() API doesn't accept a rank, so we cheat and
 * build the minimal kvs.watch request message from scratch here.
 * Read the first response and pretend we're done.  If this key actually
 * changes then we are screwed here because we will have recycled the
 * matchtags but more responses will be coming.
 */
void send_watch_requests (flux_t h, const char *key)
{
    JSON in = Jnew ();
    flux_rpc_t *r;
    const char *json_str;

    json_object_object_add (in, "noexist", NULL);
    Jadd_bool (in, ".flag_first", true);
    if (!(r = flux_rpc_multi (h, "kvs.watch", Jtostr (in), "all", 0)))
        err_exit ("flux_rpc_multi kvs.watch");
    while (!flux_rpc_completed (r)) {
        if (flux_rpc_get (r, NULL, &json_str) < 0)
            err_exit ("kvs.watch");
    }
    flux_rpc_destroy (r);
    Jput (in);
}

/* Sum #watchers over all ranks.
 */
int count_watchers (flux_t h)
{
    JSON out;
    const char *json_str;
    int n, count = 0;
    flux_rpc_t *r;

    if (!(r = flux_rpc_multi (h, "kvs.stats.get", NULL, "all", 0)))
        err_exit ("flux_rpc_multi kvs.stats.get");
    while (!flux_rpc_completed (r)) {
        if (flux_rpc_get (r, NULL, &json_str) < 0)
            err_exit ("kvs.stats.get");
        if (!(out = Jfromstr (json_str)) || !Jget_int (out, "#watchers", &n))
            msg_exit ("error decoding stats payload");
        count += n;
        Jput (out);
    }
    flux_rpc_destroy (r);
    return count;
}

int main (int argc, char **argv)
{
    flux_t h;
    int w0, w1, w2;

    /* Install watchers on every rank, then disconnect.
     * The number of watchers should return to the original count.
     */
    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    w0 = count_watchers (h);
    send_watch_requests (h, "nonexist");
    w1 = count_watchers (h) - w0;
    msg ("test watchers: %d", w1);
    flux_close (h);
    msg ("disconnected");

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    w2 = count_watchers (h) - w0;
    msg ("test watchers: %d", w2);
    if (w2 != 0)
        err_exit ("Test failure, watchers were not removed on disconnect");

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
