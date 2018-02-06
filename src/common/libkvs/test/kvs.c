#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs.h"
#include "kvs_private.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_namespace_create (NULL, NULL, 0, 5) == NULL && errno == EINVAL,
        "flux_kvs_namespace_create fails on bad input");

    errno = 0;
    ok (flux_kvs_namespace_remove (NULL, NULL) == NULL && errno == EINVAL,
        "flux_kvs_namespace_remove fails on bad input");

    errno = 0;
    ok (flux_kvs_get_version (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_get_version fails on bad input");

    errno = 0;
    ok (flux_kvs_wait_version (NULL, 0) < 0 && errno == EINVAL,
        "flux_kvs_wait_version fails on bad input");
}

void namespace (void)
{
    const char *str;

    ok (setenv ("FLUX_KVS_NAMESPACE", "FOOBAR", 1) == 0,
        "setenv FLUX_KVS_NAMESPACE success");
    ok ((str = get_kvs_namespace ()) != NULL,
        "get_kvs_namespace returns non-NULL");
    ok (!strcmp (str, "FOOBAR"),
        "get_kvs_namespace returns correct non-default namespace");
    ok (unsetenv ("FLUX_KVS_NAMESPACE") == 0,
        "unsetenv FLUX_KVS_NAMESPACE success");
    ok ((str = get_kvs_namespace ()) != NULL,
        "get_kvs_namespace returns non-NULL");
    ok (!strcmp (str, KVS_PRIMARY_NAMESPACE),
        "get_kvs_namespace returns correct default namespace");
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    errors ();
    namespace ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

