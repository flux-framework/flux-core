#include <flux/core.h>
#include "src/common/libutil/shortjson.h"

void get_rank (flux_rpc_t *rpc, void *arg)
{
    const char *json_str;
    JSON o;
    const char *rank;

    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        err_exit ("flux_rpc_get");
    if (!(o = Jfromstr (json_str)) || !Jget_str (o, "value", &rank))
        msg_exit ("response protocol error");
    printf ("rank is %s\n", rank);
    Jput (o);
    flux_rpc_destroy (rpc);
}

int main (int argc, char **argv)
{
    flux_t h;
    flux_rpc_t *rpc;
    JSON o = Jnew ();

    Jadd_str (o, "name", "rank");
    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (!(rpc = flux_rpc (h, "cmb.attrget", Jtostr (o), FLUX_NODEID_ANY, 0)))
        err_exit ("flux_rpc");
    if (flux_rpc_check (rpc))
        get_rank (rpc, NULL);
    else if (flux_rpc_then (rpc, get_rank, NULL))
        err_exit ("flux_rpc_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        err_exit ("flux_reactor_run");

    flux_close (h);
    Jput (o);
    return (0);
}
