#include <flux/core.h>
#include "src/common/libutil/shortjson.h"

void get_rank (flux_rpc_t *rpc, void *arg)
{
    const char *json_str;
    JSON o;
    int rank;
    uint32_t nodeid;

    if (flux_rpc_get (rpc, &nodeid, &json_str) < 0)
        err_exit ("flux_rpc_get");
    if (!(o = Jfromstr (json_str)) || !Jget_int (o, "rank", &rank))
        msg_exit ("response protocol error");
    printf ("[%u] rank is %d\n", nodeid, rank);
    Jput (o);
}

int main (int argc, char **argv)
{
    flux_t h;
    flux_rpc_t *rpc;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (!(rpc = flux_rpc_multi (h, "cmb.info", NULL, "all", 0)))
        err_exit ("flux_rpc");
    if (flux_rpc_then (rpc, get_rank, NULL) < 0)
        err_exit ("flux_rpc_then");
    if (flux_reactor_start (h) < 0)
        err_exit ("flux_reactor_start");

    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}
