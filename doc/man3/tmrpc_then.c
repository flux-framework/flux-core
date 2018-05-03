#include <inttypes.h>
#include <flux/core.h>
#include "src/common/libutil/log.h"

void get_rank (flux_mrpc_t *mrpc, void *arg)
{
    const char *rank;
    uint32_t nodeid;

    if (flux_mrpc_get_nodeid (mrpc, &nodeid) < 0)
        log_err_exit ("flux_mrpc_get_nodeid");
    if (flux_mrpc_get_unpack (mrpc, "{s:s}", "value", &rank) < 0)
        log_err_exit ("flux_mrpc_get");
    printf ("[%" PRIu32 "] rank is %s\n", nodeid, rank);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_mrpc_t *mrpc;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(mrpc = flux_mrpc_pack (h, "attr.get", "all", 0,
				 "{s:s}", "name", "rank")))
        log_err_exit ("flux_mrpc");
    if (flux_mrpc_then (mrpc, get_rank, NULL) < 0)
        log_err_exit ("flux_mrpc_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_mrpc_destroy (mrpc);
    flux_close (h);
    return (0);
}
