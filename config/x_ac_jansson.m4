# Versions of jansson shipped by distros:
# sles15-sp5    ships 2.9  (flux-framework/flux-core#5544)
# centos7/TOSS3 ships 2.10 (flux-framework/flux-core#3239)
# centos8/TOSS4 ships 2.11
# Ubuntu 18.04  ships 2.11
# Ubuntu 20.04  ships 2.12
#
# Some modern jansson features used in flux-core:
# - json_pack ("O?")     from 2.8
# - json_string_length() from 2.7
#
AC_DEFUN([X_AC_JANSSON], [

    PKG_CHECK_MODULES([JANSSON], [jansson >= 2.9], [], [])

    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $JANSSON_LIBS"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $JANSSON_CFLAGS"

    AX_COMPILE_CHECK_SIZEOF(json_int_t, [#include <jansson.h>])

    AS_VAR_IF([ac_cv_sizeof_json_int_t],[8],,[
        AC_MSG_ERROR([json_int_t must be 64 bits for flux to be built])
    ])

    AX_COMPILE_CHECK_SIZEOF(int)

    AS_VAR_IF([ac_cv_sizeof_int],[2],[
        AC_MSG_ERROR([flux cannot be built on a system with 16 bit ints])
    ])

    AC_REPLACE_FUNCS(json_object_update_recursive)

    LIBS="$ac_save_LIBS"
    CFLAGS="$ac_save_CFLAGS"
  ]
)
