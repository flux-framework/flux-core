/* test server main function */
typedef int (*test_server_f)(flux_t *h, void *arg);

/* create a test server running on a shmem:// connector */
flux_t *test_server_create (test_server_f cb, void *arg);

/* send shutdown message, join with thread */
int test_server_stop (flux_t *c);
