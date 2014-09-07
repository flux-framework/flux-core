#ifndef _HAVE_ZDUMP_H
#define _HAVE_ZDUMP_H

/* Format message frames as text.  The first prints entire message on stdout.
 * The second returns a string representing only routing frames that the
 * caller must free.
 */
void zdump_fprint (FILE *f, zmsg_t *self, const char *prefix);
char *zdump_routestr (zmsg_t *zmsg, int skiphops);

#endif /* !_HAVE_ZDUMP_H */
