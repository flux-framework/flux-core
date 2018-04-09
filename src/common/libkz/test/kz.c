#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kz.h"
#include "src/common/libtap/tap.h"

void abuse_args (void)
{
    errno = 0;
    ok (kz_set_ready_cb (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "kz_set_ready_cb kz=NULL fails with EINVAL");

    ok (kz_close (NULL) == 0,
        "kz_close kz=NULL succeeds");

    errno = 0;
    ok (kz_flush (NULL) < 0 && errno == EINVAL,
        "kz_flush kz=NULL fails with EINVAL");

    errno = 0;
    ok (kz_get (NULL, NULL) < 0 && errno == EINVAL,
        "kz_get kz=NULL fails with EINVAL");

    errno = 0;
    ok (kz_get_json (NULL) == NULL && errno == EINVAL,
        "kz_get_json kz=NULL fails with EINVAL");

    errno = 0;
    ok (kz_put (NULL, NULL, 0) < 0 && errno == EINVAL,
        "kz_put kz=NULL fails with EINVAL");

    errno = 0;
    ok (kz_put_json (NULL, NULL) < 0 && errno == EINVAL,
        "kz_put_json kz=NULL fails with EINVAL");

    errno = 0;
    ok (kz_open (NULL, NULL, 0) == NULL && errno == EINVAL,
        "kz_open kz=NULL fails with EINVAL");

}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    abuse_args ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

