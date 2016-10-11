#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

/* The req.clog request will not be answered until req.flush is called.
 */
void stuck_request_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    flux_rpc_t *rpc;
    int saved_errno, rc = -1;

    if (!(rpc = flux_rpc (h, "req.clog", NULL, FLUX_NODEID_ANY, 0))) {
        saved_errno = errno;
        flux_log_error (h, "%s: req.clog request", __FUNCTION__);
        goto done;
    }
    if (flux_rpc_get (rpc, NULL) < 0) {
        saved_errno = errno;
        flux_log_error (h, "%s: req.clog response", __FUNCTION__);
        goto done;
    }
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "%s: responding", __FUNCTION__);
    flux_rpc_destroy (rpc);
}

void hi_request_cb (flux_t *h, flux_msg_handler_t *w,
                    const flux_msg_t *msg, void *arg)
{
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: responding", __FUNCTION__);
}


struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "coproc.stuck",           stuck_request_cb },
    { FLUX_MSGTYPE_REQUEST, "coproc.hi",              hi_request_cb },
    FLUX_MSGHANDLER_TABLE_END,
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t *h, int argc, char **argv)
{
    int saved_errno;
    flux_flags_set (h, FLUX_O_COPROC);

    if (flux_msg_handler_addvec (h, htab, NULL) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_msg_handler_addvec");
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        saved_errno = errno;
        flux_log_error (h, "flux_reactor_start");
        flux_msg_handler_delvec (htab);
        goto error;
    }
    flux_msg_handler_delvec (htab);
    return 0;
error:
    errno = saved_errno;
    return -1;
}

MOD_NAME ("coproc");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
