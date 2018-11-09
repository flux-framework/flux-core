#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "util.h"

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    test_server_environment_init ("log-test");

    if (!(h = test_server_create (NULL, NULL)))
        BAIL_OUT ("could not create test server");
    if (flux_attr_fake (h, "rank", "0", FLUX_ATTRFLAG_IMMUTABLE) < 0)
        BAIL_OUT ("flux_attr_fake failed");

    errno = 1234;
    flux_log_error (h, "hello world");
    ok (errno == 1234,
        "flux_log_error didn't clobber errno");

    errno = 1236;
    flux_log (h, LOG_INFO, "errlo orlk");
    ok (errno == 1236,
       "flux_log didn't clobber errno");

    test_server_stop (h);
    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

