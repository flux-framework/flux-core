#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

/* The req.clog request will not be answered until req.flush is called.
 */
static int stuck_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    if (flux_json_rpc (h, FLUX_NODEID_ANY, "req.clog", NULL, NULL) < 0) {
        flux_log (h, LOG_ERR, "%s: req.clog RPC: %s", __FUNCTION__,
                  strerror (errno));
        return -1;
    }
    if (flux_err_respond (h, 0, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: responding: %s", __FUNCTION__,
                  strerror (errno));
        return -1;
    }
    return 0;
}

static int hi_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    if (flux_err_respond (h, 0, zmsg) < 0) {
        flux_log (h, LOG_ERR, "%s: responding: %s", __FUNCTION__,
                  strerror (errno));
        return -1;
    }
    return 0;
}


static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST, "coproc.stuck",           stuck_request_cb },
    { FLUX_MSGTYPE_REQUEST, "coproc.hi",              hi_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    flux_flags_set (h, FLUX_O_COPROC);

    if (flux_msghandler_addvec (h, htab, htablen, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("coproc");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
