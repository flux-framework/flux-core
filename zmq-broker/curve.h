#ifndef FLUX_CURVE_H
#define FLUX_CURVE_H 1

/* N.B. all of these functions print errors using err ().
 */

/* Get the path to the user's .curve directory.
 * Caller must free returned string.
 * Returns string on success, NULL on failure.
 */
char *flux_curve_getpath (void);

/* Given path to user's .curve directory, check that it's a directory,
 * owned by the user, and group and other have no access.
 * If create is true, create the directory if it doesn't exist.
 * Returns 0 on success, -1 on failure.
 */
int flux_curve_checkpath (const char *path, bool create);

/* Load the client/server cert from the directory 'path', for session name
 * 'session' ("flux" if NULL).
 * Caller must zcert_destroy () the returned certificate.
 * Returns cert on success, NULL on failure.
 */
zcert_t *flux_curve_getcli (const char *path, const char *session);
zcert_t *flux_curve_getsrv (const char *path, const char *session);

/* (Re)generate the client/server cert in the directory 'path',
 * for session name 'session' ("flux" if NULL).
 * If 'force' is true, unlink any old certs first.
 * Returns 0 on success, -1 on failure.
 */
int flux_curve_gencli (const char *path, const char *session, bool force);
int flux_curve_gensrv (const char *path, const char *session, bool force);

#endif /*FLUX_CURVE_H*/

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
