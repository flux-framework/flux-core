#
# FIXME - this is just a placeholder for a real zeromq/czmq macro
#
AC_DEFUN([X_AC_ZEROMQ], [
    X_AC_CHECK_COND_LIB(zmq, zmq_init)
    AS_VAR_IF([ac_cv_lib_zmq_zmq_init],[yes],,[
        AC_MSG_ERROR([no suitable zmq library found])
    ])

    CPPFLAGS="$CPPFLAGS -I/usr/include/czmq"
    X_AC_CHECK_COND_LIB(czmq, zhash_new)
    AS_VAR_IF([ac_cv_lib_czmq_zhash_new],[yes],,[
        AC_MSG_ERROR([no suitable czmq library found])
    ])
    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $LIBZMQ $LIBCZMQ"
    AC_MSG_CHECKING([For CURVE encryption support in libzmq])
    AC_CACHE_VAL(ac_cv_curve_support,
      AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <zmq.h>
#include <zmq_utils.h>
  main()
  {
    char x[[41]], y[[41]];
    /*
     * Check for CURVE support in current version of libzmq
     */
    int rc = zmq_curve_keypair (x, y);
    exit (rc >= 0 ? 0 : 1);
  }
]])],
    ac_cv_curve_support=yes,
    ac_cv_curve_support=no,
    ac_cv_curve_support=no))

    AC_MSG_RESULT($ac_cv_curve_support)
    if test "x$ac_cv_curve_support" = "xno"; then
      AC_MSG_ERROR([
 Failed to detect CURVE support in libzmq!
 Perhaps you need to compile with libsodium?])
    fi
    LIBS="$ac_save_LIBS"
  ]
)
