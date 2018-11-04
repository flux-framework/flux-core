typedef struct content_cache content_cache_t;

int content_cache_set_flux (content_cache_t *cache, flux_t *h);

content_cache_t *content_cache_create (void);
void content_cache_destroy (content_cache_t *cache);

int content_cache_register_attrs (content_cache_t *cache, attr_t *attr);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
