/* issue1760.c - make kvs module sad */

/* Failure mode 1:
./issue1760 a
2018-10-25T13:09:25.940817Z kvs.alert[0]: dropped 12 of 12 cache entries
2018-10-25T13:09:25.941349Z kvs.err[0]: load: content_load_request_send: Invalid argument
2018-10-25T13:09:25.941367Z kvs.err[0]: kvstxn_load_cb: load: Invalid argument
issue1760: flux_future_get: Invalid argument
 */

/* Failure mode 2:
./issue1760 a.b.c
2018-10-25T13:13:04.091577Z kvs.alert[0]: dropped 14 of 14 cache entries
flux-broker: kvs.c:640: load: Assertion `ret == 1' failed.
2018-10-25T13:13:04.092399Z kvs.err[0]: load: content_load_request_send: Invalid argument
issue1760: flux_future_get: Success
Aborted (core dumped)
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    const char *dir;
    char key[256];

    if (argc != 2) {
        fprintf (stderr, "Usage issue1760 dirpath");
        exit (1);
    }
    dir = argv[1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* Mkdir <dir>
     */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_mkdir (txn, 0, dir) < 0)
        log_err_exit ("flux_kvs_txn_mkdir");
    if (!(f = flux_kvs_commit (h, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    /* Expire internal kvs cache
     */
    if (flux_kvs_dropcache (h) < 0)
        log_err_exit ("flux_kvs_dropcache");

    /* Commit the following:
     * put <dir>.a
     * unlink <dir>
     */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    snprintf (key, sizeof (key), "%s.a", dir);
    if (flux_kvs_txn_put (txn, 0, key, "42") < 0)
        log_err_exit ("flux_kvs_txn_put");
    if (flux_kvs_txn_unlink (txn, 0, dir) < 0)
        log_err_exit ("flux_kvs_txn_unlink");
    if (!(f = flux_kvs_commit (h, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    flux_close (h);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
