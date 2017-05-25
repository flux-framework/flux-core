#include <flux/core.h>
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"

void get_rank (flux_mrpc_t *mrpc, void *arg)
{
    const char *json_str;
    json_object *o;
    const char *rank;
    uint32_t nodeid;

    if (flux_mrpc_get_nodeid (mrpc, &nodeid) < 0)
        log_err_exit ("flux_mrpc_get_nodeid");
    if (flux_mrpc_get (mrpc, &json_str) < 0)
        log_err_exit ("flux_mrpc_get");
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_str (o, "value", &rank))
        log_msg_exit ("response protocol error");
    printf ("[%" PRIu32 "] rank is %s\n", nodeid, rank);
    Jput (o);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_mrpc_t *mrpc;
    json_object *o = Jnew();

    Jadd_str (o, "name", "rank");
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(mrpc = flux_mrpc (h, "attr.get", Jtostr (o), "all", 0)))
        log_err_exit ("flux_mrpc");
    if (flux_mrpc_then (mrpc, get_rank, NULL) < 0)
        log_err_exit ("flux_mrpc_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_mrpc_destroy (mrpc);
    flux_close (h);
    Jput (o);
    return (0);
}
