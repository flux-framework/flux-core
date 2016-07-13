#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t h;
    flux_rpc_t *rpc;
    int i;

    log_init ("asynfence");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (kvs_put_int (h, "test.asyncfence.a", 42) < 0)
        log_err_exit ("kvs_put test.asyncfence.a");

    if (!(rpc = kvs_fence_begin (h, "test.asyncfence.1", 1)))
        log_err_exit ("kvs_fence_begin test.asyncfence.1");

    if (kvs_put_int (h, "test.asyncfence.b", 43) < 0)
        log_err_exit ("kvs_put test.asyncfence.b");

    if (kvs_fence_finish (rpc) < 0)
        log_err_exit ("kvs_fence_finish");

    if (kvs_get_int (h, "test.asyncfence.a", &i) < 0)
        log_err_exit ("kvs_get test.asyncfence.a");
    if (i != 42)
        log_msg_exit ("test.asyncfence.a has wrong value");

    if (kvs_get_int (h, "test.asyncfence.b", &i) == 0)
        log_msg_exit ("kvs_get test.asyncfence.b worked but it shouldn't have");

    if (kvs_fence (h, "test.asyncfence.2", 1) < 0)
        log_err_exit ("kvs_fence_begin test.asyncfence.2");

    if (kvs_get_int (h, "test.asyncfence.b", &i) < 0)
        log_err_exit ("kvs_get test.asyncfence.b");
    if (i != 43)
        log_msg_exit ("test.asyncfence.b has wrong value");

    flux_close (h);
    log_fini ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
