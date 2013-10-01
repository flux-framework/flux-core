#ifndef HAVE_ROUTE_H
#define HAVE_ROUTE_H
enum { ROUTE_FLAGS_PRIVATE = 1, };

typedef struct route_ctx_struct *route_ctx_t;

route_ctx_t route_init (bool verbose);
void route_fini (route_ctx_t ctx);
void route_add (route_ctx_t ctx, const char *dst, const char *gw,
                const char *parent, int flags);
void route_del (route_ctx_t ctx, const char *dst, const char *gw);
const char *route_lookup (route_ctx_t ctx, const char *dst);

void route_add_hello (route_ctx_t ctx, zmsg_t *zmsg, int flags);
void route_del_subtree (route_ctx_t ctx, const char *rank);

json_object *route_dump_json (route_ctx_t ctx, bool private);

#endif /* !HAVE_ROUTE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
