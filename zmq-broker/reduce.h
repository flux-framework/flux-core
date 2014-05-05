typedef struct red_struct *red_t;

enum {
    FLUX_RED_AUTOFLUSH = 1, /* immediately sink after append */
};

typedef void   (*FluxRedFn)(flux_t h, zlist_t *items, void *arg);
typedef void   (*FluxSinkFn)(flux_t h, void *item, void *arg);

red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, FluxRedFn redfn,
                       int flags, void *arg);
void flux_red_destroy (red_t r);

void flux_red_flush (red_t r);

int flux_red_append (red_t r, void *item);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
