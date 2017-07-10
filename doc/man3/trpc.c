#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *rankstr;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc_pack (h, "attr.get", FLUX_NODEID_ANY, 0,
		             "{s:s}", "name", "rank")))
        log_err_exit ("flux_rpcf");

    if (flux_rpc_getf (f, "{s:s}", "value", &rankstr) < 0)
        log_err_exit ("flux_rpc_getf");

    printf ("rank is %s\n", rankstr);
    flux_future_destroy (f);

    flux_close (h);
    return (0);
}
