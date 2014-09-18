#ifndef _UTIL_ZDUMP_H
#define _UTIL_ZDUMP_H

#include <stdio.h>
#include <czmq.h>

/* Format message frames as text.  The first prints entire message on stdout.
 * The second returns a string representing only routing frames that the
 * caller must free.
 */
void zdump_fprint (FILE *f, zmsg_t *self, const char *prefix);
char *zdump_routestr (zmsg_t *zmsg, int skiphops);

#endif /* !_UTIL_ZDUMP_H */
