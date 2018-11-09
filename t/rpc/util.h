/* Test server - support brokerless testing
 *
 * Start a thread running the user-supplied function 'cb' which
 * is connected back to back to a flux_t handle returned by the
 * create function.  Run until test_server_stop() is called.
 *
 * Caveats:
 * 1) subscribe/unsubscribe requests are not supported
 * 2) all messages are sent with credentials userid=geteuid(), rolemask=OWNER
 * 3) broker attributes (such as rank and size) are unavailable
 * 4) message nodeid is ignored
 *
 * Unit tests that use the test server should probably call
 * test_server_environment_init() once to initialize czmq's runtime.
 */
typedef int (*test_server_f)(flux_t *h, void *arg);

flux_t *test_server_create (test_server_f cb, void *arg);

int test_server_stop (flux_t *c);

void test_server_environment_init (const char *test_name);
