#ifndef _FLUX_CORE_SECURITY_H
#define _FLUX_CORE_SECURITY_H

#include <stdbool.h>

#define DEFAULT_ZAP_DOMAIN      "flux"

typedef struct flux_sec_struct flux_sec_t;

struct _zctx_t;

enum {
    FLUX_SEC_TYPE_PLAIN = 1,
    FLUX_SEC_TYPE_CURVE = 2,
    FLUX_SEC_TYPE_MUNGE = 4,
    FLUX_SEC_TYPE_ALL   = (FLUX_SEC_TYPE_PLAIN | FLUX_SEC_TYPE_CURVE
                                               | FLUX_SEC_TYPE_MUNGE),
    FLUX_SEC_TYPE_FAKEMUNGE = 8, // testing only
};

/* Create a security context.
 * The default mode depends on compilation options.
 */
flux_sec_t *flux_sec_create (void);
void flux_sec_destroy (flux_sec_t *c);

/* Enable/disable/test security modes.
 */
int flux_sec_enable (flux_sec_t *c, int type);
int flux_sec_disable (flux_sec_t *c, int type);
bool flux_sec_type_enabled (flux_sec_t *c, int tm);

/* Get/set config directory used by security context.
 */
void flux_sec_set_directory (flux_sec_t *c, const char *confdir);
const char *flux_sec_get_directory (flux_sec_t *c);

/* Generate key material for configured security modes, if applicable.
 */
int flux_sec_keygen (flux_sec_t *c, bool force, bool verbose);

/* Initialize ZAUTH (PLAIN or CURVE) and MUNGE security.
 * Calling these when relevant security modes are disabled is a no-op.
 */
int flux_sec_zauth_init (flux_sec_t *c, struct _zctx_t *zctx, const char *domain);
int flux_sec_munge_init (flux_sec_t *c);

/* Enable client or server mode ZAUTH security on a zmq socket.
 * Calling these when relevant security modes are disabled is a no-op.
 */
int flux_sec_csockinit (flux_sec_t *c, void *sock);
int flux_sec_ssockinit (flux_sec_t *c, void *sock);

/* Retrieve a string describing the last error.
 * This value is valid after one of the above calls returns -1.
 * The caller should not free this string.
 */
const char *flux_sec_errstr (flux_sec_t *c);

/* Retrieve a string describing the security modes selected.
 * The caller should not free this string.
 */
const char *flux_sec_confstr (flux_sec_t *c);

/* Convert a buffer to/from a Munge credential.
 * Privacy is ensured through the use of MUNGE_OPT_UID_RESTRICTION
 * Caller must free resulting string.
 */
int flux_sec_munge (flux_sec_t *c, const char *inbuf, size_t insize,
                    char **outbuf, size_t *outsize);
int flux_sec_unmunge (flux_sec_t *c, const char *inbuf, size_t insize,
                      char **outbuf, size_t *outsize);

#endif /* _FLUX_CORE_SECURITY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
