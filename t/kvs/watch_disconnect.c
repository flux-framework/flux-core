#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libkvs/proto.h"

/* Current kvs_watch() API doesn't accept a rank, so we cheat and
 * build the minimal kvs.watch request message from scratch here.
 * Read the first response and pretend we're done.  If this key actually
 * changes then we are screwed here because we will have recycled the
 * matchtags but more responses will be coming.
 */
void send_watch_requests (flux_t *h, const char *key)
{
    json_object *in;
    flux_mrpc_t *r;

    if (!(in = kp_twatch_enc (key, NULL, KVS_PROTO_FIRST)))
        log_err_exit ("kp_twatch_enc");
    if (!(r = flux_mrpc (h, "kvs.watch", Jtostr (in), "all", 0)))
        log_err_exit ("flux_mrpc kvs.watch");
    do {
        if (flux_mrpc_get (r, NULL) < 0)
            log_err_exit ("kvs.watch");
    } while (flux_mrpc_next (r) == 0);
    flux_mrpc_destroy (r);
    Jput (in);
}

/* Sum #watchers over all ranks.
 */
int count_watchers (flux_t *h)
{
    json_object *out;
    const char *json_str;
    int n, count = 0;
    flux_mrpc_t *r;

    if (!(r = flux_mrpc (h, "kvs.stats.get", NULL, "all", 0)))
        log_err_exit ("flux_mrpc kvs.stats.get");
    do {
        if (flux_mrpc_get (r, &json_str) < 0)
            log_err_exit ("kvs.stats.get");
        if (!json_str
            || !(out = Jfromstr (json_str))
            || !Jget_int (out, "#watchers", &n))
            log_msg_exit ("error decoding stats payload");
        count += n;
        Jput (out);
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
