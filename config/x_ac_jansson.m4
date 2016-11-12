AC_DEFUN([X_AC_JANSSON], [

    PKG_CHECK_MODULES([JANSSON], [jansson >= 2.6], [], [])

    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $JANSSON_LIBS"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $JANSSON_CFLAGS"

    AX_COMPILE_CHECK_SIZEOF(json_int_t, [#include <jansson.h>])

    AS_VAR_IF([ac_cv_sizeof_json_int_t],[8],,[
        AC_MSG_ERROR([json_int_t must be 64 bits for flux to be built])
    ])

    LIBS="$ac_save_LIBS"
    CFLAGS="$ac_save_CFLAGS"
  ]
)
