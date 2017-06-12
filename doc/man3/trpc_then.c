#include <flux/core.h>
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"

void get_rank (flux_future_t *f, void *arg)
{
    const char *json_str;
    json_object *o;
    const char *rank;

    if (flux_rpc_get (f, &json_str) < 0)
        log_err_exit ("flux_rpc_get");
    if (!json_str
        || !(o = Jfromstr (json_str))
        || !Jget_str (o, "value", &rank))
        log_msg_exit ("response protocol error");
    printf ("rank is %s\n", rank);
    Jput (o);
    flux_future_destroy (f);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    json_object *o = Jnew ();

    Jadd_str (o, "name", "rank");
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "attr.get", Jtostr (o), FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_future_wait_for (f, 0.) == 0)
        get_rank (f, NULL);
    else if (flux_future_then (f, -1., get_rank, NULL))
        log_err_exit ("flux_future_then");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    Jput (o);
    return (0);
}
