enum {
    ROUTE_FLAGS_PRIVATE = 1,
};
typedef struct {
    char *gw;
    int flags;
} route_t;

typedef struct route_ctx_struct *route_ctx_t;

route_ctx_t route_init (void);
void route_fini (route_ctx_t ctx);
int route_add (route_ctx_t ctx, const char *dst, const char *gw, int flags);
void route_del (route_ctx_t ctx, const char *dst, const char *gw);
route_t *route_lookup (route_ctx_t ctx, const char *dst);

json_object *route_dump_json (route_ctx_t ctx);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
