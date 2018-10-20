#ifndef _FLUX_CORE_SECURITY_H
#define _FLUX_CORE_SECURITY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_sec_struct flux_sec_t;

enum {
    /* enabled security modes */
    FLUX_SEC_TYPE_PLAIN = 1, // cannot be used with CURVE
    FLUX_SEC_TYPE_CURVE = 2, // cannot be used with PLAIN

    /* flags */
    FLUX_SEC_VERBOSE = 0x20,
    FLUX_SEC_KEYGEN_FORCE = 0x40,
};

/* Create a security context.
 * 'typemask' (may be 0) selects the security mode and optional flags.
 * 'confdir' (may be NULL) selects a key directory.
 * This function only allocates the context and does not do anything
 * to initialize the selected security modes.  flux_sec_keygen()
 * or flux_sec_comms_init() may be called next.
 * Returns context on success, or NULL on failure with errno set.
 */
flux_sec_t *flux_sec_create (int typemask, const char *confdir);
void flux_sec_destroy (flux_sec_t *c);

/* Test whether a particular security mode is enabled
 * in the security context.
 */
bool flux_sec_type_enabled (flux_sec_t *c, int typemask);

/* Get config directory used by security context.
 * May be NULL if none was configured.
 */
const char *flux_sec_get_directory (flux_sec_t *c);

/* Generate a user's keys for the configured security modes,
 * storing them in the security context's 'confdir'.
 * If the FLUX_SEC_KEYGEN_FORCE flag is set, existing keys
 * are overwritten; otherwise the existence of keys is treated as
 * an error. This function is a no-op if no keys are required
 * by the configured security modes.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_sec_keygen (flux_sec_t *c);

/* Initialize the security context for communication.
 * For PLAIN and CURVE, a zauth
 * actor for ZAP processing is started.  Since there can be only one registered
 * zauth actor per zeromq process, this function may only be called once
 * per process.  For PLAIN, the actor is configured to allow only connections
 * from clients who can send the 'client' password stored in 'confdir'.
 * For CURVE, the actor is configured to allow only connections from
 * clients whose public keys are stored in 'confdir'.
 * The actor is not strictly necessary for client-only contexts but
 * at this point all security contexts are both client and server capable.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_sec_comms_init (flux_sec_t *c);

/* Enable the configured security mode (client role) on a
 * zeromq socket.  For PLAIN, the client password is
 * obtained from 'confdir' and associated with the socket.
 * For CURVE, the server public key and client keypair are
 * obtained from 'confdir' and associated with the socket.
 * This is a no-op if neither CURVE nor PLAIN is enabled.
 * Generallay the client role calls "connect" but this is not a
 * hard requirement for the SMTP security handshake.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_sec_csockinit (flux_sec_t *c, void *sock);

/* Enable the configured security mode (server role) on a
 * zeromq socket.  For PLAIN, plain auth is enabled for the
 * socket via the zauth actor.  For CURVE, the server keypair
 * is obtained from 'confdir' and associated with the socket,
 * and curve auth is enabled for the socket via the zauth actor.
 * This is a no-op if neither CURVE nor PLAIN is enabled.
 * Generally the server role calls "bind" but this is not a
 * hard requirement for the ZMTP security handshake.
 * Returns 0 on success, or -1 on failure with errno set.
 */
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

#ifdef __cplusplus
}
#endif

#endif /* _FLUX_CORE_SECURITY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
