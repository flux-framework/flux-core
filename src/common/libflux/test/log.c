#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");

    errno = 1234;
    flux_log_error (h, "hello world");
    ok (errno == 1234,
        "flux_log_error didn't clobber errno");

    errno = 1236;
    flux_log (h, LOG_INFO, "errlo orlk");
    ok (errno == 1236,
       "flux_log didn't clobber errno");

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

