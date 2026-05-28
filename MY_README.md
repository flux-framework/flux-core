#MY_README.md 

## Running Test Program
### Single Instance
```
flux start --setattr=log-stderr-level=7

cd ${FLUX_PMIX_DIR}/t

flux run -o verbose=2 -o pmi=pmix src/alloc
```
### Multi-Instance
```
flux start --setattr=log-stderr-level=7

flux alloc --setattr=log-stderr-level=7 -N1

cd ${FLUX_PMIX_DIR}/t

flux run -o verbose=2 -o pmi=pmix src/alloc
```


## Sequences:

## Start 
-> src/cmd/flux.c

    -> src/cmd/flux-start.c

        -> src/broker/broker.c

## Run

## Alloc
    pmix_client: PMIx_allocation_request
        pmix_server: alloc_server_callback
            pmix_plugin: threadshift, rpc('shell.dyn_alloc_request)
                shell: rpc('job_manager.dyn_alloc_request)
                    job_manager: rpc('job_manager.dyn_alloc_request)
                    ...
                    job_manager rpc('sched.dyn_alloc_request)
                        scheduler DECISION
                    job_manager: response
                    ...
                    job_manager: response
                shell: response
            pmix_plugin: callback
        pmix_server: callback
    pmix_client


## RPC
### Send an rpc request and register cb for response:
static flux_future_t *send_request (flux_t *h, struct cbdata *cbdata)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "namespace.dyn_alloc_request",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:b}",
                             "follow", 1,
                             "nobacklog", 1))
        || flux_future_then (f,
                             -1,
                             dyn_alloc_response_cb,
                             cbdata) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

### Register cb for receiving requests

flux_msg_handler_addvec -> add callbacks for rpc calls

### Unpack request

if (flux_rpc_get_unpack (f, "{s:O}", "data", &xdata) < 0){
    flux_log(ctx->h, LOG_WARNING, "pmix-alloc request: %s", future_strerror (f, errno));
    flux_respond_error(ctx->h, ctx->msg, -1, "unable to unpack response");
    goto out;
}

if(0 > (rc = flux_respond_pack(ctx->h, ctx->msg,  "{s:O}", "data", xdata))){
    flux_log(ctx->h, LOG_WARNING, "flux_respond_pack failed with %d", rc);
    flux_respond_error(ctx->h, ctx->msg, -1, "unable to pack response");
    goto out;
};

