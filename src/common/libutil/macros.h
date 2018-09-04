#ifndef LIBUTIL_MACROS_H
#define LIBUTIL_MACROS_H 1

#define REAL_STRINGIFY(X) #X
#define STRINGIFY(X) REAL_STRINGIFY (X)
#define SIZEOF_FIELD(type, field) sizeof (((type *)0)->field)

#endif
